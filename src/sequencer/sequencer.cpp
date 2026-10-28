#include "sequencer.h"
#include "IMutationOperator.h"
#include "MutationConstraints.h"
#include "MutationResult.h"
#include "TimingPolicy.h"
#include "midi_messages.h"
#include "mutations/MutationOperatorRegistry.h"
#include "Scheduler.h"
#include "ModulationApplier.h"
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

// Deterministic per-(pattern, command) mutation seed (task 14, global
// constraint: deterministic, seedable randomness). The pattern's STABLE
// identity (slot + generation), the slot ordinal and the command's inbox
// sequence number mix into splitmix64; 0 maps to 1 (GeneratorRng's
// fixed-point guard). The same command stream always replays the same
// mutations — two identical runs are bit-identical — and different
// patterns/commands draw independent streams.
uint64_t mutationSeed(common::PatternId id, uint8_t slot, SequenceId seq) {
    const uint64_t mixed = splitmix64(
        (static_cast<uint64_t>(id.index) << 40) ^
        (static_cast<uint64_t>(id.generation) << 8) ^
        (static_cast<uint64_t>(slot) << 56) ^
        static_cast<uint64_t>(seq));
    return (mixed == 0) ? 1u : mixed;
}

// Map a command's generative kind onto the operation whose three-level
// timing mode resolves its application boundary (TimingPolicy.h, T11).
GenerativeOperation generativeOperationFor(GenerativeCommandKind kind) {
    switch (kind) {
        case GenerativeCommandKind::Mutate: return GenerativeOperation::Mutate;
        case GenerativeCommandKind::Undo: return GenerativeOperation::Undo;
        case GenerativeCommandKind::Revert: return GenerativeOperation::Revert;
        case GenerativeCommandKind::Keep: return GenerativeOperation::Keep;
        case GenerativeCommandKind::None: break;
    }
    return GenerativeOperation::Mutate;  // unreachable (None is never staged)
}

// Decode one command-queue generative command (ui-integration T6) into the
// fixed POD passenger the generative dispatch machinery consumes — the SAME
// InboxCommand staging the CommandInbox path feeds, but with NO inbox
// sequence (kInvalidSequence): the inter-core FIFO carries no ack
// handshake, so nothing is marked. MUTATE resolves the operator INDEX
// through the sequencer core's OWN registry (both cores list the built-ins
// in the same registration order; the UI never touches this registry) and
// carries the FR7 target mask, the FR6 intensity and the packed FR10
// level-1 override; the three recovery commands carry the override alone.
// Returns kind None when the operator index names nothing registered (the
// caller then refuses the command untouched).
GenerativeCommand generativeFromMessage(const commands::CommandMessage& msg) {
    GenerativeCommand gen{};
    gen.timing.active = commands::generativeTimingActive(msg.genTiming);
    gen.timing.mode = commands::generativeTimingMode(msg.genTiming);
    switch (msg.cmd) {
        case commands::Command::MUTATE: {
            const IMutationOperator* op = g_mutationRegistry.at(msg.param1);
            if (op == nullptr) return gen;  // kind stays None: unknown index
            gen.kind = GenerativeCommandKind::Mutate;
            copyMutationOpId(gen.mutate.opId, op->id());
            gen.mutate.patternIndex = msg.param2;
            gen.mutate.targets = msg.mutateTargets;
            gen.mutate.intensity = msg.mutateIntensity;
            return gen;
        }
        case commands::Command::UNDO:
            gen.kind = GenerativeCommandKind::Undo;
            return gen;
        case commands::Command::REVERT:
            gen.kind = GenerativeCommandKind::Revert;
            return gen;
        case commands::Command::KEEP:
            gen.kind = GenerativeCommandKind::Keep;
            return gen;
        default:
            return gen;
    }
}

// The global BPM address (task 17): the ONE parameter the transport's
// tick-duration math consumes. While a modulation overlay is armed here,
// update() opens a "tempo window" — the timeline's BPM runs at the bus's
// EFFECTIVE value for the window only, restored to the stored base around
// it — so a modulated tempo never compounds and never leaks into the
// stored state (AC3).
constexpr ParameterAddress kBpmAddress{ParameterScope::Global, 0, ParamId::GlobalBpm};

// --- modulation target configuration commands (ui-integration T7) -----------
//
// The ModulationPatternView's ONLY cross-core path: the view keeps its OWN
// local UI mirror of the target configuration and posts every edit as a
// MOD_TARGET_ADD / MOD_TARGET_SET_PARAM command. The handlers below resolve
// the payloads against the sequencer core's OWN ModulationRegistry — the
// fixed pool the tick-path ModulationApplier reads (g_modulationRegistry).
// No target object and no registry crosses the cores; the two sides only
// agree on the command vocabulary (command.h), the slot/pattern indices and
// the compile-time parameter table (ParameterAddress.h).

// Apply one MOD_TARGET_ADD: claim the first free slot of the process-wide
// registry and bind it to the command's global modulation pattern. A full
// registry is a graceful refusal (never a block, never a growth); the
// pattern index was validated by canApplyCommand.
void applyModTargetAdd(uint8_t patternIndex) {
    const size_t slot = g_modulationRegistry.addTarget();
    if (slot == ModulationRegistry::INVALID_INDEX) return;  // full: refuse
    g_modulationRegistry.target(slot).patternIndex = patternIndex;
}

// Apply one MOD_TARGET_SET_PARAM: write ONE field of one ACTIVE target slot
// (the slot and the field ordinal were validated by canApplyCommand; the
// value byte is defensively clamped/refused per field — a foreign value
// never reaches the tick path).
void applyModTargetSetParam(uint8_t slot, uint8_t field, uint8_t value) {
    ModulationTarget& target = g_modulationRegistry.target(slot);
    switch (static_cast<commands::ModTargetField>(field)) {
        case commands::ModTargetField::ParamId: {
            // The table's scope rides along, so an explicitly selected
            // parameter is always VALIDLY addressed (by construction).
            if (value >= kParamIdCount) return;  // unknown table id: refuse
            target.address.paramId = static_cast<ParamId>(value);
            target.address.scope = kParamInfos[value].scope;
            return;
        }
        case commands::ModTargetField::Scope: {
            const ParameterScope scope = static_cast<ParameterScope>(value);
            if (scope != ParameterScope::Pattern && scope != ParameterScope::Global)
                return;  // foreign ordinal: refuse
            const ParamInfo* info =
                paramInfoOf(static_cast<uint16_t>(target.address.paramId));
            if (info != nullptr && info->scope != scope) {
                // Re-point a scope-mismatched parameter so the address
                // stays VALID (the same rule the UI mirror enforces on its
                // side): global keeps only the BPM, per-pattern falls back
                // to the first per-pattern parameter (GateSteps).
                target.address.paramId = (scope == ParameterScope::Global)
                                             ? ParamId::GlobalBpm
                                             : static_cast<ParamId>(1);
            }
            target.address.scope = scope;
            if (scope == ParameterScope::Pattern &&
                target.address.patternIndex >= common::PATTERN_CAPACITY) {
                target.address.patternIndex = 0;  // defensive clamp
            }
            return;
        }
        case commands::ModTargetField::AddressPatternSlot:
            // The per-pattern slot for Pattern-scope addresses (FR8: the
            // pattern the target lives on); a foreign slot is refused.
            if (value >= common::PATTERN_CAPACITY) return;
            target.address.patternIndex = value;
            return;
        case commands::ModTargetField::PatternIndex:
            // The driving global modulation pattern (FR9): a target can be
            // re-bound to any of the fixed patterns.
            if (value >= ModulationRegistry::MAX_PATTERNS) return;
            target.patternIndex = value;
            return;
        case commands::ModTargetField::ScaleFactor:
            target.scaleFactor = (value > 127) ? 127 : value;  // clamp
            return;
        case commands::ModTargetField::Balance: {
            // Signed byte: the view packs a negative balance as its two's-
            // complement byte (command.h), this side signs it again.
            int signedValue = static_cast<int8_t>(value);
            if (signedValue < -127) signedValue = -127;
            if (signedValue > 127) signedValue = 127;
            target.balance = static_cast<int8_t>(signedValue);
            return;
        }
        case commands::ModTargetField::DurationLength:
            // FR14 hold length in the step-duration's unit, 1-16.
            if (value == 0) {
                target.stepDuration.length = 1;
            } else if (value > 16) {
                target.stepDuration.length = 16;
            } else {
                target.stepDuration.length = value;
            }
            return;
        case commands::ModTargetField::DurationUnit: {
            const StepUnit unit = static_cast<StepUnit>(value);
            if (unit != StepUnit::Beat && unit != StepUnit::Bar) return;
            target.stepDuration.unit = unit;
            return;
        }
        case commands::ModTargetField::Order: {
            const ModulationOrder order = static_cast<ModulationOrder>(value);
            if (order != ModulationOrder::Forward && order != ModulationOrder::Reverse &&
                order != ModulationOrder::PingPong && order != ModulationOrder::Random)
                return;  // foreign ordinal: refuse
            target.order = order;
            return;
        }
    }
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
    // Populate the mutation operator registry the same way (task 14): the
    // dispatch resolves operator ids through the process-wide registry —
    // the ONLY coupling point to the mutation plugins (T10's list is the
    // only built-in listing; add()'s duplicate guard makes this safe).
    registerBuiltInMutations(g_mutationRegistry);

    // Bind the ParameterBus at construction (task 17): the tick loop reads
    // EFFECTIVE values (modulation-overlay aware) from this bus before every
    // step evaluation, so it is bound to the timeline and working state from
    // the start, not only when a caller first touches parameterBus()
    // (bind is idempotent; the lazy accessor keeps its re-bind).
    parameterBus_.bind(&timeline, &workingState_);
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

    // Modulation (task 17): the power-on baseline carries no modulation
    // overlays — effective values equal the stored bases until the tick
    // loop re-arms them from the configuration.
    parameterBus_.clearAllOverlays();

    for (auto& pattern : patterns) {
        pattern.getGateSet().reset();
        pattern.getPitchSet().reset();
        pattern.getVelocitySet().reset();
    }
    // Generative RUNTIME positions restart from their defined start; the
    // generative CONFIGURATION (the working state) is Working State and is
    // preserved (task 3). Staged quantized commands belong to the previous
    // timeline epoch and are dropped (task 14).
    resetGenerativeRuntime();
    clearPendingGenerative();
}

void Sequencer::update() {
    // Publish the sounding-note count every call — BEFORE the not-playing early
    // return — so it reflects Stop's releaseAll() (0 after Stop) rather than
    // freezing at the last played value. Single aligned 32-bit store, read
    // lock-free by core0 (task 12.4 literal stuck-note metric).
    g_soundingNotes = noteLifecycle.soundingCount();

    // Drain the bounded command inbox (task 14, Req 7.4): at most
    // kGenerativeDrainBudget commands leave the ring per call — never
    // blocking, never unbounded. An Immediate generative command executes
    // here, i.e. before the next serviced tick (and also while the transport
    // is STOPPED, so the recovery commands Undo/Revert/Keep always answer);
    // a quantized one stages in the pending slot list for its resolved
    // boundary tick (three-level policy, AC7).
    drainCommandInbox();

    if (!playing) return;

    // All musical time advances through Scheduler::service(now) (tasks 5.1-5.3,
    // Req 3.1-3.8). The scheduler runs immediate safety work first, advances
    // every due tick up to its documented per-service work budget, reconciles
    // missed ticks below one beat WITHOUT obsolete Note On output, and signals a
    // fault when lateness reaches one beat.
    const uint64_t currentTime = clock.nowMicros();

    // TEMPO WINDOW (task 17, FR12, AC3): while a BPM modulation overlay is
    // armed, the transport's tick-duration math runs at the EFFECTIVE tempo
    // read from the ParameterBus — the same source step evaluation reads. The
    // timeline's stored BPM is swapped to the effective value for this window
    // only and RESTORED afterwards (and to the stored base inside each tick
    // callback, before the modulation pass recomputes the overlay), so every
    // tick's overlay is derived from the user's stored base and a modulated
    // tempo can never compound; baseValue(BPM) — what the UI, undo and
    // persistence read — is never modified. setBpm is phase-preserving: same
    // semantics as a user tempo change, the new duration applies from the
    // next tick, so late-tick reconciliation keeps working unchanged (AC3).
    const bool tempoModulated = parameterBus_.hasOverlay(kBpmAddress);
    const uint16_t storedBpm = timeline.bpm();
    if (tempoModulated) {
        timeline.setBpm(
            static_cast<uint16_t>(parameterBus_.effectiveValue(kBpmAddress)));
    }

    auto safety = []() { /* reserved: pending safety-op draining (tasks 6.x) */ };
    auto tick = [this, storedBpm, tempoModulated](uint64_t /*intendedMicros*/) {
        instrumentation::PerformanceTrace::tickBegin();
        // Restore the STORED base first (task 17): the modulation pass inside
        // serviceTick must recompute this tick's overlay from the user's
        // base, never from a previously modulated tempo. Then re-sync the
        // freshly computed effective BPM so the NEXT tick's duration runs at
        // the modulated tempo.
        if (tempoModulated) timeline.setBpm(storedBpm);
        // A staged command resolved to THIS tick applies before the tick's
        // emission, so the boundary tick itself plays the mutated material
        // (task 14, AC7).
        applyDueGenerativeCommands();
        serviceTick(/*reconcileOnly=*/false);
        if (tempoModulated) {
            timeline.setBpm(
                static_cast<uint16_t>(parameterBus_.effectiveValue(kBpmAddress)));
        }
        instrumentation::PerformanceTrace::tickEnd();
    };
    auto reconcile = [this, storedBpm, tempoModulated](uint64_t /*intendedMicros*/) {
        // Missed tick: advance phase and emit crossed releases only (Req 3.5),
        // no obsolete Note On and no obsolete Clock byte (Req 3.4/3.6). A
        // staged command resolved to this tick still applies exactly here —
        // deterministically, whenever the transport catches up (task 14).
        // The same tempo protocol as the tick callback (task 17): restore the
        // stored base before the pass, re-sync the recomputed effective BPM
        // after it, so a multi-tick catch-up window never compounds either.
        if (tempoModulated) timeline.setBpm(storedBpm);
        applyDueGenerativeCommands();
        serviceTick(/*reconcileOnly=*/true);
        if (tempoModulated) {
            timeline.setBpm(
                static_cast<uint16_t>(parameterBus_.effectiveValue(kBpmAddress)));
        }
    };

    Scheduler<decltype(safety), decltype(tick), decltype(reconcile)> scheduler(timeline,
                                                                               metrics);
    const ServiceReport report =
        scheduler.service(currentTime, std::move(safety), std::move(tick), std::move(reconcile));

    // Close the tempo window (task 17): outside it the timeline always holds
    // the STORED base — baseValue(BPM) reads exactly the user's value.
    if (tempoModulated) {
        timeline.setBpm(storedBpm);
    }

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
    // (Req 10.6), not here, so Working State inspection after a fault is
    // stable. A latched fault ends the timeline's tick epoch: staged
    // quantized commands were resolved against it, so none may fire against
    // a restarted transport (task 14).
    clearPendingGenerative();

    // Modulation (task 17): the fail-closed state carries no overlays —
    // effective values fall back to the stored bases (a faulted transport
    // reads pristine Working State, AC3). The next explicit Play re-arms
    // them from the current configuration.
    parameterBus_.clearAllOverlays();
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

    // MODULATION PASS (task 17, FR11-FR14, AC3): run for THIS tick, at the
    // single authoritative transport position, BEFORE step evaluation — so
    // the effective values resolved below (material traversal, scale, and
    // the tempo through update()'s window) belong to this tick. Bounded by
    // the registry's fixed target capacity, allocation-free, deterministic;
    // writes ONLY ParameterBus overlays — never a base value, an undo
    // snapshot or a saved baseline. Reconciled ticks run the same pass at
    // their own positions, so late-tick catch-up stays deterministic (no
    // step is skipped or replayed by the reconciliation itself).
    ModulationApplier::process(transportPositionOf(timeline), g_modulationRegistry,
                               parameterBus_);

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
        const PatternGenerativeState* gstate = workingState_.working(
            static_cast<uint8_t>(i));
        if (gstate != nullptr && gstate->anyGeneratorAssigned()) {
            dispatchGenerativeStep(i, generativeSteps_[i], gen);
        }

        // EFFECTIVE material (task 17): the material sets' SIZE and PLAYING
        // ORDER resolve through the ParameterBus — overlay-aware, so
        // modulating them re-traverses the live set — while the CONTENTS
        // stay the working state's (modulation never edits Working State,
        // AC3). With nothing modulated this is exactly the stored set: the
        // bus's clamp is the identity on the stored count/order invariants.
        GenerativeValueSet pitchMaterial{};
        GenerativeValueSet velocityMaterial{};
        if (gstate != nullptr) {
            const uint8_t slot = static_cast<uint8_t>(i);
            pitchMaterial = gstate->pitchSet;
            velocityMaterial = gstate->velocitySet;
            pitchMaterial.count = static_cast<uint8_t>(parameterBus_.effectiveValue(
                {ParameterScope::Pattern, slot, ParamId::PitchSetCount}));
            pitchMaterial.order = static_cast<common::PlayingOrder>(
                parameterBus_.effectiveValue(
                    {ParameterScope::Pattern, slot, ParamId::PitchPlayingOrder}));
            velocityMaterial.count = static_cast<uint8_t>(
                parameterBus_.effectiveValue(
                    {ParameterScope::Pattern, slot, ParamId::VelocitySetCount}));
            velocityMaterial.order = static_cast<common::PlayingOrder>(
                parameterBus_.effectiveValue(
                    {ParameterScope::Pattern, slot, ParamId::VelocityPlayingOrder}));
        }

        const bool gateFromGenerator = gen.gateResolved;
        // Source checks (task 3 / task 14): each role participates from its
        // generator (when one resolved and produced a decision), from the
        // WORKING STATE's material set (the live source the mutation
        // commands edit — the very next Note On plays the edited material),
        // or from the stored pattern data. With no generators assigned and
        // empty working sets this reduces EXACTLY to the Milestone 2
        // participation rule (all three stored sets non-empty), so existing
        // behavior is preserved.
        const bool pitchFromState =
            !gen.pitchResolved && gstate != nullptr && pitchMaterial.count > 0;
        const bool velocityFromState =
            !gen.velocityResolved && gstate != nullptr && velocityMaterial.count > 0;
        if (!gateFromGenerator && gateSet.getGates().empty()) continue;
        if (!gen.pitchResolved && !pitchFromState && pitchSet.getPitches().empty()) continue;
        if (!gen.velocityResolved && !velocityFromState &&
            velocitySet.getVelocities().empty())
            continue;

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
                // falls back to the WORKING STATE's live material set (the
                // mutation substrate, task 14) and then to the stored pattern
                // data unchanged (task 3).
                const uint8_t pitch =
                    gen.pitchResolved ? gen.pitch
                    : pitchFromState
                        ? materialValueAt(pitchMaterial, statePitchCursor_[i])
                        : pitchSet.getPitch();
                const uint8_t velocity =
                    gen.velocityResolved ? gen.velocity
                    : velocityFromState
                        ? materialValueAt(velocityMaterial, stateVelocityCursor_[i])
                        : velocitySet.getVelocity();
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
            // The material that produced this onset advances on its falling
            // edge — the effective material view when it was the source
            // (the working state's live set traversed at its effective
            // size/order, task 17), the stored set otherwise — with
            // PitchSet-compatible traversal (position / playing order /
            // pendulum / LCG).
            if (pitchFromState) {
                advanceMaterial(pitchMaterial, statePitchCursor_[i]);
            } else {
                pitchSet.advance();
            }
            if (velocityFromState) {
                advanceMaterial(velocityMaterial, stateVelocityCursor_[i]);
            } else {
                velocitySet.advance();
            }
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
    if (patternIndex >= common::PATTERN_CAPACITY) return nullptr;
    return workingState_.working(static_cast<uint8_t>(patternIndex));
}

const PatternGenerativeState* Sequencer::generativeState(size_t patternIndex) const {
    if (patternIndex >= common::PATTERN_CAPACITY) return nullptr;
    return workingState_.working(static_cast<uint8_t>(patternIndex));
}

void Sequencer::dispatchGenerativeStep(size_t patternIndex, uint32_t stepIndex,
                                       GenerativeStep& step) {
    const PatternGenerativeState* stateSlot =
        workingState_.working(static_cast<uint8_t>(patternIndex));
    if (stateSlot == nullptr) return;  // defensive: callers stay in range
    const PatternGenerativeState& state = *stateSlot;
    // The pattern length the generator context reports: the stored gate set's
    // tick length (scaffold for step-based generators), clamped to the
    // context's uint8 field.
    const uint32_t storedLength = patterns[patternIndex].getGateSet().getLength();
    const uint8_t patternLength =
        static_cast<uint8_t>(storedLength > 255u ? 255u : storedLength);
    // The scale constraint resolves through the ParameterBus from the
    // GLOBAL single source (ui-integration T4, FR12/FR13/FR14): the
    // transport timeline's stored scale, read at the Global-scope
    // addresses — overlay-aware, so a Global-scope modulation of the
    // scale shifts pitch generation live (AC10/AC11) — while the timeline's
    // STORED scale (what the UI, undo and persistence read) is untouched
    // (AC3). With nothing modulated this is exactly timeline.scale(): the
    // bus's clamp is the identity on the stored invariants, so every
    // pattern reads the ONE global scale and a change affects all
    // patterns immediately (FR14).
    const common::Scale scale(
        static_cast<uint8_t>(parameterBus_.effectiveValue(
            {ParameterScope::Global, 0, ParamId::GlobalScaleRoot})),
        static_cast<uint8_t>(parameterBus_.effectiveValue(
            {ParameterScope::Global, 0, ParamId::GlobalScaleTemplate})));

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
        statePitchCursor_[i] = MaterialCursor{};
        stateVelocityCursor_[i] = MaterialCursor{};
    }
}

// --- generative command dispatch (task 14, FR5/FR9/FR10, AC4-AC7) -------------

void Sequencer::drainCommandInbox() {
    InboxCommand cmd;
    for (uint16_t n = 0; n < kGenerativeDrainBudget && inbox_.tryPop(cmd); ++n) {
        if (cmd.gen.kind != GenerativeCommandKind::None) {
            stageGenerativeCommand(cmd);
        }
        // Ordinary (transport) inbox operations keep the Accepted status
        // recorded at pop time; their wiring is the transport integration
        // (later tasks) and costs a bounded pop here.
    }
}

void Sequencer::stageGenerativeCommand(const InboxCommand& cmd) {
    // Three-level timing resolution (T11, AC7): the invocation's override,
    // else the operation's configured mode, else the global default.
    const GenerativeOperation op = generativeOperationFor(cmd.gen.kind);
    const timing_policy::TimingOverride invocation{
        cmd.gen.timing.active,
        static_cast<OperationTimingMode>(cmd.gen.timing.mode)};
    const OperationTimingMode mode =
        timing_policy::resolveTiming(op, timingConfig_, invocation);
    if (mode == OperationTimingMode::Immediate) {
        // Immediate applies on the next serviced tick — i.e. it executes NOW,
        // before that tick is serviced (and works while stopped, so the
        // recovery commands always answer).
        executeGenerativeCommand(cmd.gen, cmd.sequence);
        return;
    }
    const uint64_t applyTick = timing_policy::computeApplyTick(mode, timeline);
    if (pendingGenerativeCount_ >= kMaxPendingGenerativeCommands) {
        // Bounded drop (AC4): the fixed pending list is full — refuse the
        // NEWEST staged command and ack it Rejected rather than blocking,
        // growing memory or evicting an older (sooner) command. A FIFO
        // command carries no sequence (kInvalidSequence) and marks nothing.
        if (cmd.sequence != kInvalidSequence) {
            inbox_.markAck(cmd.sequence, AckStatus::Rejected);
        }
        return;
    }
    PendingGenerative& slot = pendingGenerative_[pendingGenerativeCount_++];
    slot.applyTick = applyTick;
    slot.sequence = cmd.sequence;
    slot.gen = cmd.gen;
}

void Sequencer::applyDueGenerativeCommands() {
    const uint64_t currentTick = timeline.tickIndex();
    uint8_t write = 0;
    for (uint8_t i = 0; i < pendingGenerativeCount_; ++i) {
        if (timing_policy::isDue(pendingGenerative_[i].applyTick, currentTick)) {
            // Apply in staging order; executed commands leave the list.
            executeGenerativeCommand(pendingGenerative_[i].gen,
                                     pendingGenerative_[i].sequence);
        } else {
            pendingGenerative_[write++] = pendingGenerative_[i];  // keep, in order
        }
    }
    pendingGenerativeCount_ = write;
}

void Sequencer::executeGenerativeCommand(const GenerativeCommand& gen, SequenceId seq) {
    switch (gen.kind) {
        case GenerativeCommandKind::Mutate:
            executeMutate(gen.mutate, seq);
            return;
        case GenerativeCommandKind::Undo:
            workingState_.undo();  // AC5: pops LIFO; a no-op when the stack is empty
            if (seq != kInvalidSequence) inbox_.markAck(seq, AckStatus::Applied);
            return;
        case GenerativeCommandKind::Revert:
            workingState_.revert();  // AC6: every pattern's baseline, stack cleared
            if (seq != kInvalidSequence) inbox_.markAck(seq, AckStatus::Applied);
            return;
        case GenerativeCommandKind::Keep:
            workingState_.keep();  // FR15: re-anchor the baseline, stack cleared
            if (seq != kInvalidSequence) inbox_.markAck(seq, AckStatus::Applied);
            return;
        case GenerativeCommandKind::None:
            return;  // never staged; ignore
    }
    if (seq != kInvalidSequence) {
        inbox_.markAck(seq, AckStatus::Rejected);  // unknown kind fails closed
    }
}

void Sequencer::executeMutate(const MutateCmd& mutate, SequenceId seq) {
    // Lookup by id through the registry ONLY (T10): the single coupling
    // point to the mutation plugins; no concrete operator type is named
    // here. An unknown (or empty) id rejects the command untouched.
    IMutationOperator* op = g_mutationRegistry.find(mutate.opId);
    if (op == nullptr) {
        if (seq != kInvalidSequence) inbox_.markAck(seq, AckStatus::Rejected);
        return;
    }
    bool anyExecuted = false;
    if (mutate.patternIndex == kAllPatterns) {
        // FR5's all-patterns form: fan out over the live slots in order;
        // every application snapshots its own undo step (AC5).
        for (size_t i = 0; i < patterns.size(); ++i) {
            anyExecuted |=
                applyMutationToPattern(*op, mutate, static_cast<uint8_t>(i), seq);
        }
    } else if (static_cast<size_t>(mutate.patternIndex) < patterns.size()) {
        anyExecuted = applyMutationToPattern(*op, mutate, mutate.patternIndex, seq);
    }
    // Applied when ANY application executed (APPLIED or NO_CHANGE); only a
    // command refused everywhere (unknown target pattern, or REJECTED by
    // every operator application) acks Rejected. A FIFO command carries no
    // sequence and marks nothing.
    if (seq != kInvalidSequence) {
        inbox_.markAck(seq, anyExecuted ? AckStatus::Applied : AckStatus::Rejected);
    }
}

bool Sequencer::applyMutationToPattern(IMutationOperator& op, const MutateCmd& mutate,
                                       uint8_t slot, SequenceId seq) {
    PatternGenerativeState* working = workingState_.working(slot);
    if (working == nullptr) return false;
    // The command's FR6/FR7/FR8 parameters, verbatim: targets, intensity,
    // invariants and distance limit (the operator + finalize gate verify
    // them). The scale constraint is the GLOBAL single source
    // (ui-integration T16, FR12/FR13): the same overlay-aware Global-scope
    // read dispatchGenerativeStep uses for pitch generation, so the
    // scale-lock check and the scale-aware operators snap against exactly
    // the scale the mutated material will play under.
    MutationConstraints constraints(mutate.targets, mutate.intensity,
                                    mutate.invariants, mutate.maxDistance);
    constraints.scale = common::Scale(
        static_cast<uint8_t>(parameterBus_.effectiveValue(
            {ParameterScope::Global, 0, ParamId::GlobalScaleRoot})),
        static_cast<uint8_t>(parameterBus_.effectiveValue(
            {ParameterScope::Global, 0, ParamId::GlobalScaleTemplate})));
    const PatternGenerativeState before = *working;
    // AC5: the snapshot is pushed BEFORE the operator runs, so Undo always
    // holds the pre-mutation state — whatever the operator then does.
    workingState_.recordBeforeMutation(slot);
    PatternMutationView view(*working, before, slot);
    // Deterministic per-(pattern, command) seed (global constraint): the
    // same command stream always replays the same mutations.
    GeneratorRng rng(mutationSeed(patternIds[slot], slot, seq));
    const MutationResult result = op.apply(view, constraints, rng);
    if (!result.applied()) {
        // Only APPLIED mutations occupy the bounded history: a NO_CHANGE or
        // REJECTED command did not touch the working state, so its snapshot
        // (the most recent push) is cancelled instead of burning undo depth
        // on a no-op step.
        UndoEntry cancelled;
        workingState_.undoStack().pop(cancelled);
    }
    // Executed, not refused: APPLIED and NO_CHANGE both executed the
    // command (a NO_CHANGE is a successful no-op and acks Applied); only a
    // REJECTED operator refuses it.
    return result.status != MutationStatus::REJECTED;
}

void Sequencer::clearPendingGenerative() {
    // Bounded by the fixed list capacity (at most 8 entries): the epoch that
    // was to execute these commands ended (fault latch / transport restart),
    // so each dropped command's producer gets a FINAL Rejected status instead
    // of being left in the pop-time Accepted limbo. Newest-first is
    // irrelevant — the sequences are independent.
    while (pendingGenerativeCount_ > 0) {
        --pendingGenerativeCount_;
        const PendingGenerative& dropped = pendingGenerative_[pendingGenerativeCount_];
        if (dropped.sequence != kInvalidSequence) {
            inbox_.markAck(dropped.sequence, AckStatus::Rejected);
        }
    }
}

// Working-state material traversal (task 14): common::PitchSet-compatible
// semantics over a fixed GenerativeValueSet. valueAt reads
// values[position] (defensively re-based when a mutation or direct write
// shrank the set below the cursor); advance mirrors PitchSet::advance per
// playing order. Bounded, allocation-free.
uint8_t Sequencer::materialValueAt(const GenerativeValueSet& set,
                                   const MaterialCursor& cursor) {
    const uint8_t count =
        set.count < kGenerativeSetCapacity ? set.count : kGenerativeSetCapacity;
    if (count == 0) return 0;
    uint8_t position = cursor.position;
    if (position >= count) position = 0;
    return set.values[position];
}

void Sequencer::advanceMaterial(const GenerativeValueSet& set, MaterialCursor& cursor) {
    const uint8_t count =
        set.count < kGenerativeSetCapacity ? set.count : kGenerativeSetCapacity;
    if (count == 0) return;
    cursor.previous = cursor.position;
    if (cursor.position >= count) cursor.position = 0;  // a shrunken set re-bases
    if (count == 1) {
        cursor.position = 0;
        return;
    }
    switch (set.order) {
        case common::PlayingOrder::FORWARDS:
            cursor.position = static_cast<uint8_t>((cursor.position + 1) % count);
            break;
        case common::PlayingOrder::BACKWARDS:
            cursor.position = static_cast<uint8_t>((cursor.position + count - 1) % count);
            break;
        case common::PlayingOrder::PENDULUM:
            if (cursor.pendulum > 0 && cursor.position >= count - 1) {
                cursor.pendulum = -1;
            } else if (cursor.pendulum < 0 && cursor.position == 0) {
                cursor.pendulum = 1;
            }
            cursor.position = static_cast<uint8_t>(cursor.position + cursor.pendulum);
            break;
        case common::PlayingOrder::RANDOM:
            cursor.lcg = cursor.lcg * 1103515245u + 12345u;
            cursor.position = static_cast<uint8_t>(((cursor.lcg >> 16) & 0xFF) % count);
            break;
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
        case commands::Command::SCALE_SET:
            // The global scale's single source (ui-integration T4, FR12/
            // FR13): the transport timeline. setScale applies the (root,
            // template) pair to the ONE place every pattern's pitch
            // generation reads from (FR14), rejecting an invalid pair and
            // leaving the previous scale untouched (Req 1.7) — the same
            // reject-not-round contract as BPM.
            timeline.setScale(msg.scaleRoot, msg.scaleTemplateIndex);
            break;
        case commands::Command::PATTERN_SCALE_SET:
            // ScaleSelectionView's only write path (ui-integration T16):
            // the per-pattern scale override no longer exists — the scale
            // is the ONE global, so the command's (root, template) pair
            // commits to the same single source as SCALE_SET (FR12/FR13),
            // affecting every pattern immediately (FR14). The pattern index
            // stays a validated command argument (canApplyCommand) but
            // selects nothing per-pattern anymore.
            timeline.setScale(msg.scaleRoot, msg.scaleTemplateIndex);
            break;
        case commands::Command::GEN_PARAM_SET:
            setGeneratorParameter(msg.param1, msg.param2, msg.paramValue);
            break;
        case commands::Command::MUTATE:
        case commands::Command::UNDO:
        case commands::Command::REVERT:
        case commands::Command::KEEP: {
            // The command queue's generative commands (ui-integration T6):
            // decode the fixed POD passenger and route it through the SAME
            // staging / execution machinery the CommandInbox dispatch uses
            // (three-level timing resolution, bounded pending list, undo
            // history). A FIFO command carries no inbox sequence
            // (kInvalidSequence), so nothing is ever ack-marked on it.
            InboxCommand staged{};
            staged.gen = generativeFromMessage(msg);
            if (staged.gen.kind == GenerativeCommandKind::None) {
                break;  // unknown operator index: refuse untouched
            }
            stageGenerativeCommand(staged);
            break;
        }
        case commands::Command::MOD_TARGET_ADD:
            // ui-integration T7: claim a target slot bound to the command's
            // global modulation pattern (see applyModTargetAdd).
            applyModTargetAdd(msg.param1);
            break;
        case commands::Command::MOD_TARGET_SET_PARAM:
            // ui-integration T7: write one field of one active target slot
            // (see applyModTargetSetParam).
            applyModTargetSetParam(commands::modTargetSlotOf(msg.param1),
                                   commands::modTargetFieldOf(msg.param1),
                                   msg.param2);
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
        case commands::Command::SCALE_SET:
            // Always applicable: the timeline's setScale performs the range
            // check and rejects an invalid (root, template) pair without
            // touching state (Req 1.7).
            return true;
        case commands::Command::PATTERN_SCALE_SET:
            return msg.param1 < patterns.size();
        case commands::Command::GEN_PARAM_SET:
            return msg.param1 < patterns.size();
        case commands::Command::UNDO:
        case commands::Command::REVERT:
        case commands::Command::KEEP:
            // Recovery gestures always answer: an empty undo stack is a
            // defined no-op and the pending list stays bounded, so there is
            // nothing to refuse here.
            return true;
        case commands::Command::MUTATE:
            // The operator index must name one of the built-in operators
            // (both cores list the same registration order) and the pattern
            // a live slot — or the ALL_PATTERNS fan-out sentinel.
            return g_mutationRegistry.at(msg.param1) != nullptr &&
                   (msg.param2 == commands::ALL_PATTERNS ||
                    static_cast<size_t>(msg.param2) < patterns.size());
        case commands::Command::MOD_TARGET_ADD:
            // The driving pattern must name one of the fixed global
            // modulation patterns; a FULL registry is a graceful refusal
            // in the handler (never a block, never a growth).
            return msg.param1 < ModulationRegistry::MAX_PATTERNS;
        case commands::Command::MOD_TARGET_SET_PARAM:
            // The field must name a known member and the slot an ACTIVE
            // target; the value byte is clamped/refused per field in the
            // handler.
            return commands::modTargetFieldOf(msg.param1) <
                       commands::MOD_TARGET_FIELD_COUNT &&
                   g_modulationRegistry.targetActive(
                       commands::modTargetSlotOf(msg.param1));
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
        case commands::Command::SCALE_SET:
            timeline.setScale(msg.scaleRoot, msg.scaleTemplateIndex); break;
        case commands::Command::PATTERN_SCALE_SET:
            // Same single-source target as SCALE_SET (ui-integration T16):
            // the scale is global; the pattern index selects nothing.
            timeline.setScale(msg.scaleRoot, msg.scaleTemplateIndex); break;
        case commands::Command::GEN_PARAM_SET:
            setGeneratorParameter(msg.param1, msg.param2, msg.paramValue);
            break;
        case commands::Command::MUTATE:
        case commands::Command::UNDO:
        case commands::Command::REVERT:
        case commands::Command::KEEP: {
            // The generative commands (ui-integration T6) carry no vector
            // payload, so the allocation-free path applies them exactly like
            // the scalar path — one shared staging / execution path for both
            // receive paths (one pending list, one three-level timing
            // resolution, one undo history). No inbox sequence accompanies
            // a FIFO command, so nothing is ever ack-marked on it.
            InboxCommand staged{};
            staged.gen = generativeFromMessage(msg);
            if (staged.gen.kind == GenerativeCommandKind::None) {
                break;  // unknown operator index: refuse untouched
            }
            stageGenerativeCommand(staged);
            break;
        }
        case commands::Command::MOD_TARGET_ADD:
            // ui-integration T7: same handler on the allocation-free core1
            // path — the payload is single-word, so the snapshot is unused.
            applyModTargetAdd(msg.param1);
            break;
        case commands::Command::MOD_TARGET_SET_PARAM:
            applyModTargetSetParam(commands::modTargetSlotOf(msg.param1),
                                   commands::modTargetFieldOf(msg.param1),
                                   msg.param2);
            break;
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

    // Staged quantized commands were resolved against the PREVIOUS epoch's
    // tick indices; a fresh timeline must not fire them at unrelated ticks,
    // so they are dropped (deterministic and bounded) (task 14). This runs
    // only on an actual restart — the repeated-Play early return above keeps
    // a playing transport's staged commands.
    clearPendingGenerative();

    // Modulation (task 17): a fresh timeline epoch starts from the stored
    // bases — no overlays carried over from the previous epoch. The tick
    // loop re-arms the overlay set from the current configuration on the
    // first serviced tick.
    parameterBus_.clearAllOverlays();

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

    // Modulation (task 17): a stopped transport carries no modulation
    // overlays — effective values equal the stored bases until the next
    // Play re-arms them (the bus's documented play/stop overlay reset).
    parameterBus_.clearAllOverlays();
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
    // assigned (task 3). The manager's working AND baseline slots are default
    // outside the live range (removePattern re-anchors both whenever slots
    // shift), so no manager action is needed — only the slot-local runtime
    // restarts below.
    const size_t slot = patterns.size() - 1;
    *workingState_.working(static_cast<uint8_t>(slot)) = PatternGenerativeState{};
    generativeSteps_[slot] = 0;
    generativeGateLast_[slot] = false;
    generativeDurationLeft_[slot] = 0;
    statePitchCursor_[slot] = MaterialCursor{};
    stateVelocityCursor_[slot] = MaterialCursor{};
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
        // The state is trivially copyable, so slot moves are plain
        // assignments (working state, runtime, material cursors).
        *workingState_.working(static_cast<uint8_t>(j)) =
            *workingState_.working(static_cast<uint8_t>(j + 1));
        generativeSteps_[j] = generativeSteps_[j + 1];
        generativeGateLast_[j] = generativeGateLast_[j + 1];
        generativeDurationLeft_[j] = generativeDurationLeft_[j + 1];
        statePitchCursor_[j] = statePitchCursor_[j + 1];
        stateVelocityCursor_[j] = stateVelocityCursor_[j + 1];
    }
    *workingState_.working(static_cast<uint8_t>(surviving)) = PatternGenerativeState{};
    generativeSteps_[surviving] = 0;
    generativeGateLast_[surviving] = false;
    generativeDurationLeft_[surviving] = 0;
    statePitchCursor_[surviving] = MaterialCursor{};
    stateVelocityCursor_[surviving] = MaterialCursor{};
    // Structural change (task 14): slot indices shifted, so the undo
    // history's pattern indices no longer name their patterns, and the
    // baseline's vacated slots are stale. Re-anchor the baseline at the
    // current (shifted) working state and drop the invalidated history —
    // Keep-style semantics for structural edits (T12/T13).
    workingState_.captureBaseline();
    workingState_.undoStack().clear();
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

void Sequencer::setGeneratorParameter(uint8_t patternIndex, uint8_t paramId,
                                       int16_t value) {
    // The generator-parameter span of the task-16 table (GateSteps ..
    // DurationVariation): GEN_PARAM_SET writes exactly the parameters the
    // GeneratorConfigView edits; anything else is refused untouched.
    const ParamId id = static_cast<ParamId>(paramId);
    if (id < ParamId::GateSteps || id > ParamId::DurationVariation) return;
    // A plain BASE write through the bus (the same surface the view used to
    // call directly): the stored per-pattern generative working state
    // changes, clamped into the parameter's UI range; no modulation
    // overlay is ever armed (overlays belong to the modulation layer).
    ParameterAddress address;
    address.scope = ParameterScope::Pattern;
    address.patternIndex = patternIndex;
    address.paramId = id;
    parameterBus_.setBase(address, value);
}

void Sequencer::sendMidiByte(uint8_t byte) {
    // Transport bytes (Start/Clock/Stop) emit through the same instrumented
    // path as note bytes so counting and tracing stay consistent.
    meteredOutput.write(byte);
}

}  // namespace sequencer
