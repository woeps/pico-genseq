#pragma once

#include <cstdint>
#include <vector>

#include "GeneratorRegistry.h"
#include "IMidiOutput.h"
#include "IMonotonicClock.h"
#include "NoteLifecycle.h"
#include "PatternGenerativeState.h"
#include "PerformanceFault.h"
#include "PerformanceMetrics.h"
#include "TransportTimeline.h"
#include "hardware/uart.h"
#include "../commands/command.h"
#include "../common/pattern.h"
#include "../common/PatternId.h"

namespace sequencer {

class PatternSnapshot;  // fwd (task 6.4 allocation-free receive path)

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

    // Per-pattern generative state (task 3): each pattern owns ONE fixed POD
    // holding its generator assignments (per role), generator parameters,
    // scale constraint (FR4) and generative material sets. Returns nullptr
    // when patternIndex is out of the fixed slot range. This is Working
    // State configuration: it is changed only by performer commands, never
    // automatically, and enters undo/baseline/Keep snapshots (T12/T13).
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
    // generated-gate edge memory, duration countdown). The generative
    // CONFIGURATION (generativeStates_) is Working State and is preserved.
    void resetGenerativeRuntime();

    // Registry every generator id is resolved through (task 3).
    GeneratorRegistry& generatorRegistry_;
    // Per-pattern generative state, parallel to `patterns` (fixed slots —
    // no heap; addPattern/removePattern keep the slots aligned).
    PatternGenerativeState generativeStates_[common::PATTERN_CAPACITY] = {};
    // Per-pattern generative RUNTIME, parallel to generativeStates_: the
    // step counter fed to generate(), the last generated gate value for
    // edge detection, and the armed auto-release countdown (transport ticks)
    // a DURATION generator set at Note On. Runtime only — never snapshotted.
    uint32_t generativeSteps_[common::PATTERN_CAPACITY] = {};
    bool generativeGateLast_[common::PATTERN_CAPACITY] = {};
    uint16_t generativeDurationLeft_[common::PATTERN_CAPACITY] = {};

    void sendMidiByte(uint8_t byte);
    bool canApplyCommand(const commands::CommandMessage& msg) const;
};

// Function to create the sequencer task for the second core
void createSequencerTask(uart_inst_t* uart, uint txPin, uint rxPin);

}  // namespace sequencer
