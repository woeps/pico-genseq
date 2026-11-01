#pragma once

#include <cstdint>
#include <vector>

#include "IMidiOutput.h"
#include "IMonotonicClock.h"
#include "NoteLifecycle.h"
#include "PatternRng.h"
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
    Sequencer(IMidiOutput& output, IMonotonicClock& clock);

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

    // Set a pattern's Generation Parameters (generative-engine-expansion). This
    // is the application point the command/boundary path routes through in task
    // 6; it is public so host tests can inject parameters directly before the
    // full command plumbing exists. Applying new parameters re-establishes the
    // pattern's generation random state so a freshly-set seed is honored.
    void setPatternGenerationParameters(size_t patternIndex,
                                        const common::GenerationParameters& params);

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
    // Per-Pattern generation random state (generative-engine-expansion task 4),
    // parallel to `patterns`. Drives probability/humanization always, and RANDOM
    // playing order ONLY when the Pattern has a Deterministic Seed set — the
    // unseeded path leaves the per-stream LCGs untouched so RANDOM output is
    // byte-identical to pre-milestone (Req 4.1/4.4).
    std::vector<PatternRng> patternRng;
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

    // Emission lens (generative-engine-expansion task 3). The single point where
    // a Pattern's Generation Parameters are applied between the stored stream
    // values and the MIDI note-on. Returns whether a note was emitted or the
    // step was suppressed (by SKIP-mode scale resolution, or — once wired in
    // tasks 4/5 — by a probability roll). A SUPPRESSED step emits no note-on and
    // strands no prior note; the caller advances stream positions exactly as the
    // unsuppressed case so phrase length and determinism are unchanged.
    enum class EmitResult { EMITTED, SUPPRESSED };
    EmitResult emitStep(size_t patternIndex, common::PatternId id,
                        common::MidiChannel channel);

    // Re-establish each Pattern's generation random state at a defined musical
    // boundary (Req 4.2/4.5): a Pattern with a Deterministic Seed resets its
    // PatternRng to seed(); a Pattern without one gets the documented default
    // seeding. Called on Play and on fault-recovery restart so seeded Patterns
    // reproduce their material from the defined starting position.
    void reseedGenerationState();

    // Fail-closed Timing Fault entry (task 5.3): release all ownership, emit Stop
    // when enabled, stop playback, and latch the cause. Working State (the
    // patterns and their configuration) is preserved; only runtime transport
    // stops. Idempotent via the PerformanceFault latch.
    void enterTimingFault(PerformanceFaultCause cause, uint64_t measurementUs);

    void sendMidiByte(uint8_t byte);
    bool canApplyCommand(const commands::CommandMessage& msg) const;
};

// Function to create the sequencer task for the second core
void createSequencerTask(uart_inst_t* uart, uint txPin, uint rxPin);

}  // namespace sequencer
