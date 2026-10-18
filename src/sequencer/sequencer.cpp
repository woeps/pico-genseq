#include "sequencer.h"
#include "midi_messages.h"
#include "Scheduler.h"
#include "PatternSnapshot.h"
#include "../common/pitch_set.h"
#include "../common/velocity_set.h"
#include "../common/gate_set.h"
#include "../common/const.h"
#include "../common/pattern.h"
#include "../instrumentation/PerformanceTrace.h"

#include <utility>

namespace sequencer {

Sequencer::Sequencer(IMidiOutput& output, IMonotonicClock& clock)
    : output(output),
      clock(clock),
      meteredOutput(output, metrics.midiBytesDispatched),
      patterns({common::Pattern()}),
      playing(false),
      midiClockEnabled(true),
      noteLifecycle(meteredOutput) {
    // MIDI transport and monotonic time are injected; no hardware setup here.
    timeline.setBpm(120);
    // Allocate a stable identity for the initial pattern slot.
    patternIds.push_back(idAllocator.allocate());
}

void MeteredMidiOutput::write(uint8_t byte) {
    instrumentation::PerformanceTrace::midiDispatchBegin();
    inner_.write(byte);
    instrumentation::PerformanceTrace::midiDispatchEnd();
    ++byteCounter_;
}

void Sequencer::init() {
    // TODO: reset values to default
}

void Sequencer::update() {
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

        if (pitchSet.getPitches().empty() || velocitySet.getVelocities().empty() ||
            gateSet.getGates().empty()) {
            continue;
        }

        const common::PatternId id = patternIds[i];
        const common::Flank flank = gateSet.getFlank();

        if (flank == common::RISING) {
            // A reconciled (missed) tick must NOT emit an obsolete Note On
            // (task 5.2, Req 3.4): the authoritative phase has already moved past
            // this onset. Advance the material so the next authoritative tick is
            // phase-correct, but emit nothing on the wire.
            if (!reconcileOnly) {
                // Record ownership at onset by the pattern's canonical channel and
                // the exact pitch sounding NOW (task 2.4, Req 5.1).
                noteLifecycle.noteOn(id, pattern.getCanonicalChannel(), pitchSet.getPitch(),
                                     velocitySet.getVelocity());
            }
        } else if (flank == common::FALLING) {
            // Release the captured onset via its recorded identity, NOT the
            // pattern's current (possibly edited) pitch (task 2.4, Req 5.2). This
            // IS emitted during reconciliation: a note whose release boundary was
            // crossed must be released so it does not hang (task 5.2, Req 3.5).
            noteLifecycle.releasePattern(id);
            pitchSet.advance();
            velocitySet.advance();
        }
        const uint32_t nextGatePosition = static_cast<uint32_t>(
            (gateSet.getPosition() + 1) % gateSet.getGates().size());
        gateSet.setPosition(nextGatePosition);
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
}

void Sequencer::removePattern(size_t index) {
    if (patterns.size() <= 1 || index >= patterns.size()) return;
    // Release every note this pattern owns BEFORE destroying its runtime state
    // so removal can never strand a sounding note (Req 5.6).
    noteLifecycle.releasePattern(patternIds[index]);
    idAllocator.release(patternIds[index]);
    patterns.erase(patterns.begin() + index);
    patternIds.erase(patternIds.begin() + index);
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
