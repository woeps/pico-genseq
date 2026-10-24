#include "sequencer.h"
#include "midi_messages.h"
#include "Scheduler.h"
#include "sequencer_telemetry.h"
#include "PatternSnapshot.h"
#include "../common/pitch_set.h"
#include "../common/velocity_set.h"
#include "../common/gate_set.h"
#include "../common/const.h"
#include "../common/pattern.h"
#include "../instrumentation/PerformanceTrace.h"

#include <utility>

namespace sequencer {

// Cross-core stuck-note telemetry (task 12.4). Written by core1 in update();
// read lock-free by core0 for H:dump.
volatile uint32_t g_soundingNotes = 0;

namespace {

// Deterministic per-(pattern, role, step) dispatch seed (global constraint:
// deterministic, seedable randomness). The pattern's STABLE identity (slot +
// generation) seeds the mix so removing/reordering patterns never retargets
// another pattern's stream; the step index distinguishes steps; the role
// ordinal keeps each role's stream independent of the other roles' draws.
// splitmix64 finalizes; 0 maps to 1 (GeneratorRng's fixed-point guard).
uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

uint64_t generatorSeed(common::PatternId id, uint8_t roleOrdinal, uint32_t stepIndex) {
    const uint64_t mixed = splitmix64(
        (static_cast<uint64_t>(id.index) << 40) ^
        (static_cast<uint64_t>(id.generation) << 8) ^
        (static_cast<uint64_t>(roleOrdinal) << 60) ^
        static_cast<uint64_t>(stepIndex));
    return (mixed == 0) ? 1u : mixed;
}

}  // namespace

Sequencer::Sequencer(IMidiOutput& output, IMonotonicClock& clock,
                     GeneratorRegistry& generatorRegistry)
    : output(output),
      clock(clock),
      meteredOutput(output, metrics.midiBytesDispatched),
      patterns({common::Pattern()}),
      playing(false),
      midiClockEnabled(true),
      noteLifecycle(meteredOutput),
      generatorRegistry_(generatorRegistry) {
    // MIDI transport and monotonic time are injected; no hardware setup here.
    timeline.setBpm(120);
    // Allocate a stable identity for the initial pattern slot.
    patternIds.push_back(idAllocator.allocate());
    // Populate the registry with the built-in generator list (task 3, AC1).
    // The ONLY call site in the core: registerGenerators is defined in
    // generators/RegisterGenerators.cpp and add()'s duplicate guard makes
    // repeated construction safe. No concrete generator type is named here.
    registerGenerators(generatorRegistry_);
}

void MeteredMidiOutput::write(uint8_t byte) {
    instrumentation::PerformanceTrace::midiDispatchBegin();
    inner_.write(byte);
    instrumentation::PerformanceTrace::midiDispatchEnd();
    ++byteCounter_;
}

void Sequencer::init() {
    // Reset runtime transport state to the defined power-on baseline. Idempotent
    // and safe to call on an already-constructed Sequencer (createSequencerTask
    // calls it once after construction; a future re-init path can call it again).
    //
    // Working State is preserved except for runtime positions: the pattern slots
    // and their identities are kept (no vector churn / reallocation), but every
    // pattern's gate/pitch/velocity runtime position is reset to its defined
    // start, playback is stopped, all note ownership is released, any latched
    // Timing Fault is acknowledged, and the default tempo is restored.
    fault.acknowledge();
    instrumentation::PerformanceTrace::setFault(false);

    playing = false;
    timeline.stop();
    noteLifecycle.releaseAll();

    midiClockEnabled = true;
    timeline.setBpm(120);

    for (auto& pattern : patterns) {
        pattern.getGateSet().reset();
        pattern.getPitchSet().reset();
        pattern.getVelocitySet().reset();
    }
    // Generative RUNTIME positions restart from their defined start; the
    // generative CONFIGURATION (generativeStates_) is Working State and is
    // preserved (task 3).
    resetGenerativeRuntime();
}

void Sequencer::update() {
    // Publish the sounding-note count every call — BEFORE the not-playing early
    // return — so it reflects Stop's releaseAll() (0 after Stop) rather than
    // freezing at the last played value. Single aligned 32-bit store, read
    // lock-free by core0 (task 12.4 literal stuck-note metric).
    g_soundingNotes = noteLifecycle.soundingCount();

    if (!playing) return;

    // All musical time advances through Scheduler::service(now) (tasks 5.1-5.3,
    // Req 3.1-3.8). The scheduler runs immediate safety work first, advances
    // every due tick up to its documented per-service work budget, reconciles
    // missed ticks below one beat WITHOUT obsolete Note On output, and signals a
    // fault when lateness reaches one beat.
    const uint64_t currentTime = clock.nowMicros();

    auto safety = []() { /* reserved: pending safety-op draining (tasks 6.x) */ };
    auto tick = [this](uint64_t /*intendedMicros*/) {
        instrumentation::PerformanceTrace::tickBegin();
        serviceTick(/*reconcileOnly=*/false);
        instrumentation::PerformanceTrace::tickEnd();
    };
    auto reconcile = [this](uint64_t /*intendedMicros*/) {
        // Missed tick: advance phase and emit crossed releases only (Req 3.5),
        // no obsolete Note On and no obsolete Clock byte (Req 3.4/3.6).
        serviceTick(/*reconcileOnly=*/true);
    };

    Scheduler<decltype(safety), decltype(tick), decltype(reconcile)> scheduler(timeline,
                                                                               metrics);
    const ServiceReport report =
        scheduler.service(currentTime, std::move(safety), std::move(tick), std::move(reconcile));

    // Fail closed at one beat of lateness (task 5.3, Req 3.7): the scheduler
    // serviced nothing this call and asked us to fault.
    if (report.faulted) {
        enterTimingFault(PerformanceFaultCause::SCHEDULER_LATENESS, report.faultLatenessUs);
    }
}

void Sequencer::enterTimingFault(PerformanceFaultCause cause, uint64_t measurementUs) {
    const FaultTransition t = fault.enter(cause, measurementUs, metrics);
    if (!t.entered) return;  // already latched — idempotent, no second cleanup

    instrumentation::PerformanceTrace::setFault(true);

    // Stop generating new Pattern events and release every owned note (Req 10.1,
    // 10.2). Working State (patterns + their configuration) is preserved; only
    // runtime transport stops.
    playing = false;
    timeline.stop();
    const bool emitStop = midiClockEnabled;
    noteLifecycle.releaseAll();

    // Emit MIDI Stop when MIDI Clock Output is enabled (Req 10.2 / 2.3).
    if (emitStop) {
        sendMidiByte(midi::SystemRealTimeMessage::STOP);
    }
    // Runtime positions are reset on the next explicit Play acknowledgement
    // (Req 10.6), not here, so Working State inspection after a fault is stable.
}

void Sequencer::serviceTick(bool reconcileOnly) {
    // MIDI Clock: at PPQN 24, 24 Clock messages per quarter note means exactly
    // one Clock per logical tick. Emitted only when the unified setting is on
    // (task 4.1, Req 2.2). A reconciled (missed) tick emits NO Clock byte — the
    // missed clock pulse is obsolete and must not be replayed in a burst
    // (task 5.2, Req 3.6).
    if (midiClockEnabled && !reconcileOnly) {
        sendMidiByte(midi::SystemRealTimeMessage::TIMING_CLOCK);
    }

    for (size_t i = 0; i < patterns.size(); ++i) {
        common::Pattern& pattern = patterns[i];
        if (!pattern.isActive()) continue;

        common::GateSet& gateSet = pattern.getGateSet();
        common::PitchSet& pitchSet = pattern.getPitchSet();
        common::VelocitySet& velocitySet = pattern.getVelocitySet();

        // Duration-driven auto-release (task 3): a DURATION generator arms a
        // bounded countdown at Note On; when the boundary is crossed the
        // recorded onset is released by its identity. Honored during
        // reconciliation like any crossed release (task 5.2, Req 3.5). Never
        // armed when no DURATION generator produced a decision, so pure
        // stored-gate playback (Milestone 2) is untouched by this path.
        if (generativeDurationLeft_[i] > 0) {
            --generativeDurationLeft_[i];
            if (generativeDurationLeft_[i] == 0) {
                noteLifecycle.releasePattern(patternIds[i]);
            }
        }

        // Generative dispatch (task 3, AC1): resolve the pattern's assigned
        // generators through the registry — the ONLY coupling point to the
        // plugins; no concrete generator type is named here. Pure stored-data
        // playback (no role assigned) skips dispatch entirely.
        GenerativeStep gen;
        if (generativeStates_[i].anyGeneratorAssigned()) {
            dispatchGenerativeStep(i, generativeSteps_[i], gen);
        }

        const bool gateFromGenerator = gen.gateResolved;
        // Source checks (task 3): each role participates from its generator
        // (when one resolved and produced a decision) or from the stored
        // pattern data. With no generators assigned this reduces EXACTLY to
        // the Milestone 2 participation rule (all three stored sets
        // non-empty), so existing behavior is preserved.
        if (!gateFromGenerator && gateSet.getGates().empty()) continue;
        if (!gen.pitchResolved && pitchSet.getPitches().empty()) continue;
        if (!gen.velocityResolved && velocitySet.getVelocities().empty()) continue;

        const common::PatternId id = patternIds[i];

        // Gate decision for this step: the GATE generator's output, edge-
        // detected against the last generated value, or — without a gate
        // generator — the stored GateSet flank exactly as before (tasks 2.4,
        // 5.2 semantics preserved). The edge memory updates on every tick,
        // including reconciled ones, so the next authoritative tick is
        // phase-correct.
        common::Flank flank;
        if (gateFromGenerator) {
            const bool was = generativeGateLast_[i];
            const bool now = gen.gate;
            if (!was && now) {
                flank = common::RISING;
            } else if (was && !now) {
                flank = common::FALLING;
            } else {
                flank = now ? common::HIGH : common::LOW;
            }
            generativeGateLast_[i] = now;
        } else {
            flank = gateSet.getFlank();
        }

        if (flank == common::RISING) {
            // A reconciled (missed) tick must NOT emit an obsolete Note On
            // (task 5.2, Req 3.4): the authoritative phase has already moved past
            // this onset. Advance the material so the next authoritative tick is
            // phase-correct, but emit nothing on the wire.
            if (!reconcileOnly) {
                // Generated material wins per role; a role without a decision
                // falls back to the stored pattern data unchanged (task 3).
                const uint8_t pitch =
                    gen.pitchResolved ? gen.pitch : pitchSet.getPitch();
                const uint8_t velocity =
                    gen.velocityResolved ? gen.velocity : velocitySet.getVelocity();
                // Record ownership at onset by the pattern's canonical channel
                // and the exact pitch sounding NOW (task 2.4, Req 5.1).
                noteLifecycle.noteOn(id, pattern.getCanonicalChannel(), pitch,
                                     velocity);
                // Arm the duration countdown when a DURATION generator
                // produced one (in transport ticks; minimum 1).
                generativeDurationLeft_[i] =
                    gen.durationResolved ? (gen.duration > 0 ? gen.duration : 1) : 0;
            }
        } else if (flank == common::FALLING) {
            // Release the captured onset via its recorded identity, NOT the
            // pattern's current (possibly edited) pitch (task 2.4, Req 5.2). This
            // IS emitted during reconciliation: a note whose release boundary was
            // crossed must be released so it does not hang (task 5.2, Req 3.5).
            noteLifecycle.releasePattern(id);
            pitchSet.advance();
            velocitySet.advance();
            // The boundary release already happened; disarm any pending
            // duration countdown so it cannot fire a second (no-op) release.
            generativeDurationLeft_[i] = 0;
        }

        // Advance the per-pattern step counter (the dispatch stepIndex) and,
        // when the gate comes from the stored set, its position — on
        // reconciled ticks too, so the authoritative phase stays correct.
        ++generativeSteps_[i];
        if (!gateFromGenerator) {
            const uint32_t nextGatePosition = static_cast<uint32_t>(
                (gateSet.getPosition() + 1) % gateSet.getGates().size());
            gateSet.setPosition(nextGatePosition);
        }
    }
}

PatternGenerativeState* Sequencer::generativeState(size_t patternIndex) {
    return (patternIndex < common::PATTERN_CAPACITY) ? &generativeStates_[patternIndex]
                                                     : nullptr;
}

const PatternGenerativeState* Sequencer::generativeState(size_t patternIndex) const {
    return (patternIndex < common::PATTERN_CAPACITY) ? &generativeStates_[patternIndex]
                                                     : nullptr;
}

void Sequencer::dispatchGenerativeStep(size_t patternIndex, uint32_t stepIndex,
                                       GenerativeStep& step) {
    const PatternGenerativeState& state = generativeStates_[patternIndex];
    // The pattern length the generator context reports: the stored gate set's
    // tick length (scaffold for step-based generators), clamped to the
    // context's uint8 field.
    const uint32_t storedLength = patterns[patternIndex].getGateSet().getLength();
    const uint8_t patternLength =
        static_cast<uint8_t>(storedLength > 255u ? 255u : storedLength);
    const common::Scale scale = state.scale();

    for (uint8_t r = 0; r < kGeneratorRoleCount; ++r) {
        const GeneratorRole role = static_cast<GeneratorRole>(r);
        if (!state.hasGenerator(role)) continue;  // unassigned: stored data
        // Lookup by id through the registry ONLY (task 3, AC1). An assigned id
        // nothing registered falls back to the stored pattern data.
        IGenerator* generator = generatorRegistry_.find(state.generatorId(role));
        if (generator == nullptr) continue;
        // Fresh deterministic RNG per (pattern, role, step): identical inputs
        // always replay identical decisions (global constraint).
        GeneratorRng rng(generatorSeed(patternIds[patternIndex], r, stepIndex));
        GeneratorContext context(patternLength, &scale, rng);
        GeneratorOutput output;
        generator->generate(stepIndex, context, output);
        // Apply the decision for THIS role only — a role never consumes
        // another role's output.
        switch (role) {
            case GeneratorRole::GATE:
                if (output.hasGate) {
                    step.gateResolved = true;
                    step.gate = output.gate;
                }
                break;
            case GeneratorRole::PITCH:
                if (output.hasPitch) {
                    step.pitchResolved = true;
                    step.pitch = static_cast<uint8_t>(output.pitch & 0x7F);
                }
                break;
            case GeneratorRole::VELOCITY:
                if (output.hasVelocity) {
                    step.velocityResolved = true;
                    step.velocity = static_cast<uint8_t>(output.velocity & 0x7F);
                }
                break;
            case GeneratorRole::DURATION:
                if (output.hasDuration) {
                    step.durationResolved = true;
                    step.duration = output.duration;
                }
                break;
        }
    }
}

void Sequencer::resetGenerativeRuntime() {
    for (uint8_t i = 0; i < common::PATTERN_CAPACITY; ++i) {
        generativeSteps_[i] = 0;
        generativeGateLast_[i] = false;
        generativeDurationLeft_[i] = 0;
    }
}

void Sequencer::processCommand(commands::CommandMessage msg) {
    if (!canApplyCommand(msg)) return;

    instrumentation::PerformanceTrace::operationApplyBegin();

    switch (msg.cmd) {
        case commands::Command::PLAY:
            play();
            break;
        case commands::Command::STOP:
            stop();
            break;
        case commands::Command::BPM_SET:
            setBPM(msg.param1);
            break;
        case commands::Command::PATTERN_ACTIVATE:
            activatePattern(msg.param1);
            break;
        case commands::Command::PATTERN_DEACTIVATE:
            deactivatePattern(msg.param1);
            break;
        case commands::Command::PATTERN_GATE_SET:
            setPatternGateSet(msg.param1, msg.gates);
            break;
        case commands::Command::PATTERN_PITCH_SET:
            setPatternPitchSet(msg.param1, msg.pitchCount, msg.pitchOrder, msg.pitches);
            break;
        case commands::Command::PATTERN_VELOCITY_SET:
            setPatternVelocitySet(msg.param1, msg.velocityCount, msg.velocityOrder,
                                  msg.velocities);
            break;
        case commands::Command::PATTERN_ADD:
            addPattern(common::Pattern());
            break;
        case commands::Command::PATTERN_REMOVE:
            removePattern(msg.param1);
            break;
        default:
            break;
    }

    ++metrics.operationsApplied;
    instrumentation::PerformanceTrace::operationApplyEnd();
}

bool Sequencer::canApplyCommand(const commands::CommandMessage& msg) const {
    switch (msg.cmd) {
        case commands::Command::PLAY:
        case commands::Command::STOP:
        case commands::Command::BPM_SET:
        case commands::Command::PATTERN_ADD:
            return true;
        case commands::Command::PATTERN_ACTIVATE:
        case commands::Command::PATTERN_DEACTIVATE:
        case commands::Command::PATTERN_GATE_SET:
            return msg.param1 < patterns.size();
        case commands::Command::PATTERN_PITCH_SET:
            return msg.param1 < patterns.size() && msg.pitchCount <= msg.pitches.size();
        case commands::Command::PATTERN_VELOCITY_SET:
            return msg.param1 < patterns.size() &&
                   msg.velocityCount <= msg.velocities.size();
        case commands::Command::PATTERN_REMOVE:
            return patterns.size() > 1 && msg.param1 < patterns.size();
        case commands::Command::NOOP:
        default:
            return false;
    }
}

void Sequencer::processCommand(const commands::CommandMessage& msg,
                               const sequencer::PatternSnapshot& snapshot) {
    // Allocation-free core1 path (task 6.4, Req 7.9). Validation and metrics are
    // identical to processCommand(msg); only the three multiword pattern-set
    // commands read their payload from `snapshot` instead of CommandMessage's
    // (now-empty) vectors. canApplyCommand checks `count <= vector.size()` for
    // set commands, which is vacuously false here (empty vectors) — so validate
    // those against the snapshot's counts instead.
    bool applicable;
    switch (msg.cmd) {
        case commands::Command::PATTERN_GATE_SET:
            applicable = msg.param1 < patterns.size();
            break;
        case commands::Command::PATTERN_PITCH_SET:
            applicable = msg.param1 < patterns.size() &&
                         msg.pitchCount <= snapshot.pitchCount();
            break;
        case commands::Command::PATTERN_VELOCITY_SET:
            applicable = msg.param1 < patterns.size() &&
                         msg.velocityCount <= snapshot.velocityCount();
            break;
        default:
            // Non-set commands carry no vector payload; the scalar path validates
            // them correctly.
            applicable = canApplyCommand(msg);
            break;
    }
    if (!applicable) return;

    instrumentation::PerformanceTrace::operationApplyBegin();

    switch (msg.cmd) {
        case commands::Command::PATTERN_GATE_SET:
            setPatternGateSetFromSnapshot(msg.param1, snapshot);
            break;
        case commands::Command::PATTERN_PITCH_SET:
            setPatternPitchSetFromSnapshot(msg.param1, snapshot);
            break;
        case commands::Command::PATTERN_VELOCITY_SET:
            setPatternVelocitySetFromSnapshot(msg.param1, snapshot);
            break;
        case commands::Command::PLAY:            play(); break;
        case commands::Command::STOP:            stop(); break;
        case commands::Command::BPM_SET:         setBPM(msg.param1); break;
        case commands::Command::PATTERN_ACTIVATE:   activatePattern(msg.param1); break;
        case commands::Command::PATTERN_DEACTIVATE: deactivatePattern(msg.param1); break;
        case commands::Command::PATTERN_ADD:     addPattern(common::Pattern()); break;
        case commands::Command::PATTERN_REMOVE:  removePattern(msg.param1); break;
        default: break;
    }

    ++metrics.operationsApplied;
    instrumentation::PerformanceTrace::operationApplyEnd();
}

void Sequencer::setPatternGateSetFromSnapshot(size_t patternIndex,
                                              const sequencer::PatternSnapshot& snap) {
    if (patternIndex >= patterns.size()) return;
    std::vector<bool> gates(snap.gateCount());
    for (uint16_t i = 0; i < snap.gateCount(); ++i) gates[i] = snap.gate(i);
    patterns[patternIndex].setGateSet(common::GateSet(gates));
}

void Sequencer::setPatternPitchSetFromSnapshot(size_t patternIndex,
                                               const sequencer::PatternSnapshot& snap) {
    if (patternIndex >= patterns.size()) return;
    std::vector<uint8_t> pitches(snap.pitchCount());
    for (uint8_t i = 0; i < snap.pitchCount(); ++i) pitches[i] = snap.pitch(i);
    patterns[patternIndex].setPitchSet(common::PitchSet(pitches, snap.pitchOrder()));
}

void Sequencer::setPatternVelocitySetFromSnapshot(size_t patternIndex,
                                                  const sequencer::PatternSnapshot& snap) {
    if (patternIndex >= patterns.size()) return;
    std::vector<uint8_t> velocities(snap.velocityCount());
    for (uint8_t i = 0; i < snap.velocityCount(); ++i) velocities[i] = snap.velocity(i);
    patterns[patternIndex].setVelocitySet(common::VelocitySet(velocities, snap.velocityOrder()));
}

void Sequencer::resetPerformanceMetrics() {
    metrics = PerformanceMetrics{};
}

void Sequencer::play() {
    // Explicit Play acknowledges a latched Timing Fault (task 5.3, Req 10.6):
    // clear the fault so playback can restart from defined positions below. The
    // acknowledge() is idempotent — a no-op when not faulted.
    const FaultTransition ack = fault.acknowledge();
    if (ack.acknowledged) {
        instrumentation::PerformanceTrace::setFault(false);
    }

    // Deterministic repeated Play (Req 5.10): a Play while already playing does
    // NOT abandon existing ownership and does NOT emit an unmatched Start/Stop.
    if (playing) return;

    // Fresh start from defined positions (Req 1.6): reset every pattern's
    // runtime positions so playback begins at the start of each cycle.
    for (auto& pattern : patterns) {
        pattern.getGateSet().reset();
        pattern.getPitchSet().reset();
        pattern.getVelocitySet().reset();
    }
    // Generative runtime restarts from step 0 with clean edge memory (task 3);
    // the generative configuration is Working State and is preserved.
    resetGenerativeRuntime();

    // Begin one monotonic Transport Timeline shared by every pattern (Req 1.1).
    timeline.start(clock.nowMicros());
    playing = true;

    // Unified MIDI Clock Output (task 4.1, Req 2.1): exactly one Start before
    // the first Clock or note event, only when enabled.
    if (midiClockEnabled) {
        sendMidiByte(midi::SystemRealTimeMessage::START);
    }
}

void Sequencer::stop() {
    const bool wasPlaying = playing;
    playing = false;
    timeline.stop();

    // Release every owned note and clear all ownership (Req 5.8).
    noteLifecycle.releaseAll();

    // Unified MIDI Clock Output (task 4.1, Req 2.3): exactly one Stop for this
    // transition to stopped, only when enabled. Emitted only if we were actually
    // playing so a redundant Stop produces no unmatched transport message.
    if (midiClockEnabled && wasPlaying) {
        sendMidiByte(midi::SystemRealTimeMessage::STOP);
    }

    // Reset every pattern's runtime positions to their defined start (Req 1.6).
    for (auto& pattern : patterns) {
        pattern.getGateSet().reset();
        pattern.getPitchSet().reset();
        pattern.getVelocitySet().reset();
    }
    // Generative runtime restarts from step 0 with clean edge memory (task 3);
    // the generative configuration is Working State and is preserved.
    resetGenerativeRuntime();
}

void Sequencer::setBPM(uint16_t bpm) {
    // Phase-preserving BPM change (Req 1.5): the timeline keeps the current tick
    // index and intended timestamp and applies the new duration from the next
    // tick. It does not restart pattern positions.
    timeline.setBpm(bpm);
}

void Sequencer::addPattern(const common::Pattern& pattern) {
    if (patterns.size() >= common::PATTERN_CAPACITY) return;  // fixed capacity
    patterns.push_back(pattern);
    patternIds.push_back(idAllocator.allocate());
    // The new slot owns a FRESH generative state: defaults, no generators
    // assigned (task 3). Runtime arrays start from their defined start.
    const size_t slot = patterns.size() - 1;
    generativeStates_[slot] = PatternGenerativeState{};
    generativeSteps_[slot] = 0;
    generativeGateLast_[slot] = false;
    generativeDurationLeft_[slot] = 0;
}

void Sequencer::removePattern(size_t index) {
    if (patterns.size() <= 1 || index >= patterns.size()) return;
    // Release every note this pattern owns BEFORE destroying its runtime state
    // so removal can never strand a sounding note (Req 5.6).
    noteLifecycle.releasePattern(patternIds[index]);
    idAllocator.release(patternIds[index]);
    patterns.erase(patterns.begin() + index);
    patternIds.erase(patternIds.begin() + index);
    // Keep the per-slot generative state and runtime aligned with the surviving
    // patterns: shift everything above the removed slot down one (task 3). The
    // state is trivially copyable, so slot moves are plain assignments.
    const size_t surviving = patterns.size();
    for (size_t j = index; j < surviving; ++j) {
        generativeStates_[j] = generativeStates_[j + 1];
        generativeSteps_[j] = generativeSteps_[j + 1];
        generativeGateLast_[j] = generativeGateLast_[j + 1];
        generativeDurationLeft_[j] = generativeDurationLeft_[j + 1];
    }
    generativeStates_[surviving] = PatternGenerativeState{};
    generativeSteps_[surviving] = 0;
    generativeGateLast_[surviving] = false;
    generativeDurationLeft_[surviving] = 0;
}

void Sequencer::activatePattern(size_t index) {
    if (index < patterns.size()) {
        patterns[index].setActive(true);
    }
}

void Sequencer::deactivatePattern(size_t index) {
    if (index < patterns.size()) {
        // Mute releases ownership at its effective boundary so a muted pattern
        // leaves no hanging note (Req 5.6).
        noteLifecycle.releasePattern(patternIds[index]);
        patterns[index].setActive(false);
    }
}

void Sequencer::setPatternGateSet(size_t patternIndex,
                                  const std::vector<bool>& gates) {
    // Set replacement is note-safe without extra work: NoteLifecycle holds the
    // sounding note by its recorded onset identity, independent of the pattern's
    // gate/pitch/velocity material, so the currently sounding note completes
    // using its original lifecycle and the replacement controls the NEXT note
    // (task 2.5, Req 5.7). An empty gate set simply produces no further edges.
    if (patternIndex < patterns.size()) {
        patterns[patternIndex].setGateSet(common::GateSet(gates));
    }
}

void Sequencer::setPatternPitchSet(size_t patternIndex, uint8_t count,
                                   common::PlayingOrder order,
                                   const std::vector<uint8_t>& pitches) {
    if (patternIndex >= patterns.size()) return;
    std::vector<uint8_t> activePitches(pitches.begin(), pitches.begin() + count);
    common::PitchSet pitchSet(activePitches, order);
    patterns[patternIndex].setPitchSet(pitchSet);
}

void Sequencer::setPatternVelocitySet(size_t patternIndex, uint8_t count,
                                      common::PlayingOrder order,
                                      const std::vector<uint8_t>& velocities) {
    if (patternIndex >= patterns.size()) return;
    std::vector<uint8_t> activeVelocities(velocities.begin(), velocities.begin() + count);
    common::VelocitySet velocitySet(activeVelocities, order);
    patterns[patternIndex].setVelocitySet(velocitySet);
}

void Sequencer::sendMidiByte(uint8_t byte) {
    // Transport bytes (Start/Clock/Stop) emit through the same instrumented
    // path as note bytes so counting and tracing stay consistent.
    meteredOutput.write(byte);
}

}  // namespace sequencer
