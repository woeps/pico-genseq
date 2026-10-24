#pragma once

#include <cstdint>
#include <vector>

#include "GeneratorRegistry.h"
#include "IMidiOutput.h"
#include "IMonotonicClock.h"
#include "CommandInbox.h"
#include "NoteLifecycle.h"
#include "OperationTimingConfig.h"
#include "PatternGenerativeState.h"
#include "PerformanceFault.h"
#include "PerformanceMetrics.h"
#include "TransportTimeline.h"
#include "WorkingStateManager.h"
#include "hardware/uart.h"
#include "../commands/command.h"
#include "../common/pattern.h"
#include "../common/PatternId.h"

namespace sequencer {

class PatternSnapshot;  // fwd (task 6.4 allocation-free receive path)
class IMutationOperator;  // fwd (task 14 mutation dispatch resolves by id)

// Instrumented MIDI output adapter: forwards every byte to the real transport
// while counting it and firing the dispatch trace markers. ALL MIDI emission —
// transport bytes via sendMidiByte and note bytes via NoteLifecycle — goes
// through one instrumented path so midiBytesDispatched and the PerformanceTrace
// dispatch markers see the complete stream (Req 3.8 / 11.5). Defined in the
// header because it is tiny and used by the Sequencer's member layout below.
class MeteredMidiOutput : public IMidiOutput {
public:
    MeteredMidiOutput(IMidiOutput& inner, uint64_t& byteCounter)
        : inner_(inner), byteCounter_(byteCounter) {}
    void write(uint8_t byte) override;  // defined in sequencer.cpp (uses PerformanceTrace)

private:
    IMidiOutput& inner_;
    uint64_t& byteCounter_;
};

// Main sequencer class
class Sequencer {
public:
    // `generatorRegistry` is the registry every per-role generator dispatch
    // (task 3, AC1) resolves ids through — the ONLY coupling point to the
    // generator plugins; the sequencer never names a concrete generator
    // type. Defaults to the process-wide registry so existing two-argument
    // construction is unchanged.
    Sequencer(IMidiOutput& output, IMonotonicClock& clock,
              GeneratorRegistry& generatorRegistry = g_generatorRegistry);

    void init();
    void update();
    void processCommand(commands::CommandMessage msg);

    // Allocation-free command application for the core1 timing path (task 6.4,
    // Req 7.9). The multiword pattern-set payloads are read from `snapshot`
    // (filled by commands::receiveCommandInto) instead of from heap-backed
    // CommandMessage vectors. Non-set commands behave exactly like
    // processCommand(msg); set commands apply from the snapshot's fixed buffers.
    void processCommand(const commands::CommandMessage& msg,
                        const sequencer::PatternSnapshot& snapshot);

    const PerformanceMetrics& performanceMetrics() const { return metrics; }
    void resetPerformanceMetrics();

    // The bounded cross-core command inbox (task 14, Req 7.1-7.4): the
    // producer pushes Mutate / Undo / Revert / Keep (and, later, transport)
    // operations here; update() drains them with a bounded budget every
    // call — never blocking, never unbounded — so the UI core can stage
    // generative requests lock-free.
    CommandInbox& commandInbox() { return inbox_; }
    const CommandInbox& commandInbox() const { return inbox_; }

    // The working-state manager the generative commands operate on (T12/
    // T13, FR9/FR15): per-pattern working state, the saved baseline, the ONE
    // global undo stack. Mutate snapshots through recordBeforeMutation
    // (AC5); Undo / Revert / Keep execute here directly (AC6).
    WorkingStateManager& workingStateManager() { return workingState_; }
    const WorkingStateManager& workingStateManager() const { return workingState_; }

    // The three-level timing configuration (T11, FR10): level-2 per-
    // operation modes plus the level-3 global default every generative
    // command resolves through (TimingPolicy::resolveTiming) when no
    // per-invocation override (level 1) is supplied.
    OperationTimingConfig& operationTiming() { return timingConfig_; }
    const OperationTimingConfig& operationTiming() const { return timingConfig_; }

    // Number of quantized generative commands currently staged in the fixed
    // pending slot list (bounded by kMaxPendingGenerativeCommands). Runtime
    // observability only — Working State lives in workingStateManager().
    uint8_t pendingGenerativeCount() const { return pendingGenerativeCount_; }

    // Per-pattern generative state (task 3, task 14): each pattern owns ONE
    // fixed POD holding its generator assignments (per role), generator
    // parameters, scale constraint (FR4) and generative material sets.
    // Returns nullptr when patternIndex is out of the fixed slot range.
    // This is the WORKING state the generative commands mutate and the
    // dispatch plays from (T12/T13): it is changed only by performer
    // commands, never automatically, and enters undo/baseline/Keep snapshots
    // through workingStateManager().
    PatternGenerativeState* generativeState(size_t patternIndex);
    const PatternGenerativeState* generativeState(size_t patternIndex) const;

private:
    IMidiOutput& output;
    IMonotonicClock& clock;
    PerformanceMetrics metrics{};
    // Instrumented wrapper around `output`; constructed before noteLifecycle so
    // the note service emits through the same counted/traced path.
    MeteredMidiOutput meteredOutput;
    std::vector<common::Pattern> patterns;
    // Stable identity for each pattern slot, parallel to `patterns`. Note
    // ownership binds to these ids, so removing/reordering a pattern never
    // retargets another pattern's sounding note (tasks 2.2, 2.4).
    std::vector<common::PatternId> patternIds;
    common::PatternIdAllocator idAllocator;
    bool playing;
    bool midiClockEnabled;

    // Global musical timeline (task 3.2) and the single note-ownership service
    // all emission passes through (task 2.3).
    TransportTimeline timeline;
    NoteLifecycle noteLifecycle;
    // Latched fail-closed Timing Fault (task 5.3). Entering it stops new events,
    // releases ownership, optionally emits Stop, and prohibits auto-restart; an
    // explicit Play acknowledges and restarts (Req 10.1-10.7).
    PerformanceFault fault;

    void play();
    void stop();
    void setBPM(uint16_t bpm);
    void sendMidiClock();

    void addPattern(const common::Pattern& pattern);
    void removePattern(size_t index);
    void activatePattern(size_t index);
    void deactivatePattern(size_t index);
    void setPatternGateSet(size_t patternIndex, const std::vector<bool>& gates);
    void setPatternPitchSet(size_t patternIndex, uint8_t count,
                            common::PlayingOrder order,
                            const std::vector<uint8_t>& pitches);
    void setPatternVelocitySet(size_t patternIndex, uint8_t count,
                               common::PlayingOrder order,
                               const std::vector<uint8_t>& velocities);

    // Snapshot-reading appliers (task 6.4): construct the replacement set from
    // the fixed-capacity PatternSnapshot buffers, so no CommandMessage vector is
    // populated on the core1 receive path. Each replaces ONLY its own set,
    // preserving the pattern's other material (the wire sends gate/pitch/velocity
    // as separate commands).
    void setPatternGateSetFromSnapshot(size_t patternIndex,
                                       const sequencer::PatternSnapshot& snap);
    void setPatternPitchSetFromSnapshot(size_t patternIndex,
                                        const sequencer::PatternSnapshot& snap);
    void setPatternVelocitySetFromSnapshot(size_t patternIndex,
                                           const sequencer::PatternSnapshot& snap);

    // Advance one logical tick. When reconcileOnly is true the tick is a MISSED
    // tick being reconciled (task 5.2): pattern phase advances and crossed
    // releases are emitted, but NO obsolete Note On and NO MIDI Clock byte are
    // produced. When false it is the authoritative-phase tick (normal emission).
    void serviceTick(bool reconcileOnly = false);

    // Fail-closed Timing Fault entry (task 5.3): release all ownership, emit Stop
    // when enabled, stop playback, and latch the cause. Working State (the
    // patterns and their configuration) is preserved; only runtime transport
    // stops. Idempotent via the PerformanceFault latch.
    void enterTimingFault(PerformanceFaultCause cause, uint64_t measurementUs);

    // One step's registry-dispatch result (task 3): which roles produced a
    // decision and what the decision was. A small fixed POD — no heap.
    struct GenerativeStep {
        bool gateResolved = false;
        bool gate = false;
        bool pitchResolved = false;
        uint8_t pitch = 0;
        bool velocityResolved = false;
        uint8_t velocity = 0;
        bool durationResolved = false;
        uint16_t duration = 0;
    };

    // Registry-based per-role generator dispatch for one pattern's step
    // (task 3, AC1): resolves the pattern's assigned ids through
    // generatorRegistry_ ONLY — no concrete generator type is named here.
    // A role participates only when it has an assigned id AND a registered
    // generator AND that generator produced a decision; otherwise the
    // sequencer falls back to the stored pattern data.
    void dispatchGenerativeStep(size_t patternIndex, uint32_t stepIndex,
                                 GenerativeStep& step);

    // Reset the per-pattern generative RUNTIME positions (step counter,
    // generated-gate edge memory, duration countdown, working-material
    // cursors). The generative CONFIGURATION (the working state itself) is
    // Working State and is preserved.
    void resetGenerativeRuntime();

    // --- generative command dispatch (task 14, FR5/FR9/FR10, AC4-AC7) -------

    // Fixed capacity of the pending quantized-command slot list (AC4: the
    // pending set is bounded memory; a full list drops the newest staged
    // command and acks it Rejected — never blocking, never unbounded).
    static constexpr uint8_t kMaxPendingGenerativeCommands = 8;
    // Documented per-update inbox drain budget (AC4, Req 7.4): at most this
    // many commands leave the ring per update() call.
    static constexpr uint16_t kGenerativeDrainBudget = 8;

    // One staged quantized command: the resolved absolute apply tick, the
    // sequence for the final ack, and the command itself. Fixed POD.
    struct PendingGenerative {
        uint64_t applyTick = 0;
        SequenceId sequence = kInvalidSequence;
        GenerativeCommand gen{};
    };

    // Drain the inbox at the top of update(): pops at most
    // kGenerativeDrainBudget commands. A command resolving to Immediate
    // executes now (i.e. before the next serviced tick — even while the
    // transport is stopped, so Undo/Revert/Keep always answer); a quantized
    // one stages in pendingGenerative_ for its boundary tick.
    void drainCommandInbox();

    // Resolve ONE popped generative command's timing (three-level policy,
    // AC7) and either execute it now (Immediate) or stage it (quantized).
    void stageGenerativeCommand(const InboxCommand& cmd);

    // Apply every staged command whose resolved tick has been reached, in
    // staging order. Called from BOTH the tick and the reconcile callback
    // with the timeline already advanced to that tick's index, so a
    // quantized command applies exactly at its boundary tick whether that
    // tick is serviced normally or reconciled late (deterministic either
    // way). Bounded by kMaxPendingGenerativeCommands.
    void applyDueGenerativeCommands();

    // Execute one generative command against the working state: Mutate runs
    // the registered operator by id; Undo / Revert / Keep run the
    // WorkingStateManager operations. Marks the final ack status.
    void executeGenerativeCommand(const GenerativeCommand& gen, SequenceId seq);

    // Mutate: resolve the operator, then apply it to the target pattern
    // slots (one, or every live pattern for kAllPatterns, in slot order).
    void executeMutate(const MutateCmd& mutate, SequenceId seq);

    // Apply one mutation to pattern slot `slot`: snapshot the working state
    // onto the global undo stack (AC5), run the operator with the command's
    // FR6/FR7/FR8 constraints and a deterministic per-(pattern, sequence)
    // PRNG, and cancel the snapshot again when the result is NO_CHANGE or
    // REJECTED (only APPLIED mutations occupy the bounded history). Returns
    // true when the operator EXECUTED the command (APPLIED or NO_CHANGE —
    // both ack Applied); false only when it refused it (REJECTED).
    bool applyMutationToPattern(IMutationOperator& op, const MutateCmd& mutate,
                                 uint8_t slot, SequenceId seq);

    // Drop every staged quantized command. Called when the timeline's tick
    // epoch ends (Play restart, Timing Fault): staged ticks belong to the
    // timeline that computed them, so a fresh epoch must not fire stale
    // boundaries. Bounded (at most kMaxPendingGenerativeCommands drops).
    void clearPendingGenerative();

    // Per-pattern runtime cursor over one WORKING-STATE material set (task
    // 14): position / previous / pendulum direction / LCG state, mirroring
    // common::PitchSet's traversal semantics over the fixed GenerativeValueSet
    // storage. Runtime only — never snapshotted, reset with the other
    // generative runtime, shifted with the pattern slots.
    struct MaterialCursor {
        uint8_t position = 0;
        uint8_t previous = 0;
        int8_t pendulum = 1;
        uint32_t lcg = 0x12345678u;
    };

    // Working-state material traversal (task 14): common::PitchSet-compatible
    // position / playing-order semantics over a fixed GenerativeValueSet,
    // implemented in sequencer.cpp.
    static uint8_t materialValueAt(const GenerativeValueSet& set,
                                   const MaterialCursor& cursor);
    static void advanceMaterial(const GenerativeValueSet& set, MaterialCursor& cursor);

    // Registry every generator id is resolved through (task 3).
    GeneratorRegistry& generatorRegistry_;
    // The bounded cross-core inbox every generative command arrives through
    // (task 14); the working state the commands edit and the dispatch plays
    // from (T12/T13); the three-level timing configuration (T11).
    CommandInbox inbox_;
    WorkingStateManager workingState_;
    OperationTimingConfig timingConfig_{};
    // Pending quantized generative commands (fixed-capacity slot list,
    // staging order) and their count.
    PendingGenerative pendingGenerative_[kMaxPendingGenerativeCommands] = {};
    uint8_t pendingGenerativeCount_ = 0;
    // Per-pattern generative RUNTIME, parallel to the working slots: the
    // step counter fed to generate(), the last generated gate value for
    // edge detection, the armed auto-release countdown (transport ticks) a
    // DURATION generator set at Note On, and the working-state material
    // cursors. Runtime only — never snapshotted.
    uint32_t generativeSteps_[common::PATTERN_CAPACITY] = {};
    bool generativeGateLast_[common::PATTERN_CAPACITY] = {};
    uint16_t generativeDurationLeft_[common::PATTERN_CAPACITY] = {};
    MaterialCursor statePitchCursor_[common::PATTERN_CAPACITY] = {};
    MaterialCursor stateVelocityCursor_[common::PATTERN_CAPACITY] = {};

    void sendMidiByte(uint8_t byte);
    bool canApplyCommand(const commands::CommandMessage& msg) const;
};

// Function to create the sequencer task for the second core
void createSequencerTask(uart_inst_t* uart, uint txPin, uint rxPin);

}  // namespace sequencer
