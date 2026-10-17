// Feature: midi-output-interface + performance-safe-foundation host smoke test
//
// Proves that the SDK-free sequencer logic compiles and links without Pico SDK
// libraries and that an injected host clock drives the production tick path at
// exact deadlines, including across the 32-bit-microsecond boundary.

#include "FakeMonotonicClock.h"
#include "IMidiOutput.h"
#include "RecordingMidiOutput.h"
#include "command.h"
#include "midi_messages.h"
#include "sequencer.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

static_assert(std::is_abstract<sequencer::IMidiOutput>::value,
              "IMidiOutput must be abstract");
static_assert(std::has_virtual_destructor<sequencer::IMidiOutput>::value,
              "IMidiOutput must have a virtual destructor");
static_assert(std::is_abstract<sequencer::IMonotonicClock>::value,
              "IMonotonicClock must be abstract");
static_assert(std::has_virtual_destructor<sequencer::IMonotonicClock>::value,
              "IMonotonicClock must have a virtual destructor");

namespace {

bool expectBytes(const std::vector<uint8_t>& actual,
                 const std::vector<uint8_t>& expected,
                 uint16_t bpm,
                 const char* phase) {
    if (actual == expected) return true;

    std::fprintf(stderr,
                 "FAIL at %s for BPM %u: got %zu bytes, expected %zu\n",
                 phase, static_cast<unsigned>(bpm), actual.size(), expected.size());
    return false;
}

bool verifyExactTickBoundary(uint16_t bpm) {
    // Begin just below 2^32 microseconds so the first tick crosses the boundary;
    // this catches accidental narrowing to a 32-bit timestamp.
    FakeMonotonicClock clock{0xFFFFFFFFULL - 1000ULL};
    RecordingMidiOutput recording;
    sequencer::Sequencer seq(recording, clock);
    seq.init();

    commands::CommandMessage gates;
    gates.cmd = commands::Command::PATTERN_GATE_SET;
    gates.param1 = 0;
    gates.gateCount = 2;
    gates.gates = {true, false};
    seq.processCommand(gates);

    commands::CommandMessage setBpm;
    setBpm.cmd = commands::Command::BPM_SET;
    setBpm.param1 = static_cast<uint8_t>(bpm);
    seq.processCommand(setBpm);

    commands::CommandMessage play;
    play.cmd = commands::Command::PLAY;
    seq.processCommand(play);

    // Rejected commands are neither traced nor counted as applied operations.
    commands::CommandMessage invalidTarget;
    invalidTarget.cmd = commands::Command::PATTERN_ACTIVATE;
    invalidTarget.param1 = 255;
    seq.processCommand(invalidTarget);

    commands::CommandMessage unknown;
    unknown.cmd = static_cast<commands::Command>(0xFF);
    seq.processCommand(unknown);

    const uint64_t base = 0xFFFFFFFFULL - 1000ULL;  // clock value captured at play()
    const uint64_t tickDurationUs = 60ULL * 1000ULL * 1000ULL / (bpm * 24ULL);
    // Exact rational deadline of logical tick n (the timeline carries the
    // division remainder so it is drift-free, task 3.2 / Req 1.5):
    //   deadline(n) = base + floor(n * 60e6 / (bpm * PPQN))
    const auto deadline = [&](uint64_t n) -> uint64_t {
        return base + (n * 60ULL * 1000ULL * 1000ULL) / (bpm * 24ULL);
    };

    // Unified MIDI Clock Output (task 4.1): PLAY emits exactly one Start and no
    // Clock yet (Clock bytes are emitted per serviced tick, below).
    if (!expectBytes(recording.record(), {sequencer::midi::SystemRealTimeMessage::START},
                     bpm, "play")) {
        return false;
    }

    // One microsecond before the first tick's exact deadline: no tick yet.
    clock.setMicros(deadline(1) - 1);
    seq.update();
    if (!expectBytes(recording.record(), {sequencer::midi::SystemRealTimeMessage::START},
                     bpm, "one microsecond before first tick")) {
        return false;
    }

    clock.setMicros(deadline(1));
    seq.update();
    // First exact tick: one Clock byte (24/qn at PPQN 24 == 1 per tick) then the
    // rising-edge Note On.
    const std::vector<uint8_t> afterRising{
        sequencer::midi::SystemRealTimeMessage::START,
        sequencer::midi::SystemRealTimeMessage::TIMING_CLOCK,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_ON),
        60,
        100,
    };
    if (!expectBytes(recording.record(), afterRising, bpm, "first exact tick")) {
        return false;
    }

    // Servicing again at the same timestamp must not produce a duplicate tick.
    seq.update();
    if (!expectBytes(recording.record(), afterRising, bpm, "duplicate service")) {
        return false;
    }

    // Service the second tick deliberately late (past its exact deadline).
    // Current behavior still processes exactly one tick, and advances from the
    // ideal deadline rather than rebasing the phase to the late observation
    // time. The falling edge releases the recorded onset note (task 2.4). A
    // Clock byte precedes it.
    constexpr uint64_t lateByUs = 500;
    clock.setMicros(deadline(2) + lateByUs);
    seq.update();
    const std::vector<uint8_t> afterFalling{
        sequencer::midi::SystemRealTimeMessage::START,
        sequencer::midi::SystemRealTimeMessage::TIMING_CLOCK,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_ON),
        60,
        100,
        sequencer::midi::SystemRealTimeMessage::TIMING_CLOCK,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_OFF),
        60,
        0,
    };
    if (!expectBytes(recording.record(), afterFalling, bpm, "late second tick")) {
        return false;
    }

    // Advancing exactly to the THIRD tick's ideal deadline services it. If the
    // sequencer had rebased phase to the late service time, this exact deadline
    // would be too early and no tick would fire — so this proves phase was
    // preserved across the late service.
    clock.setMicros(deadline(3));
    seq.update();
    const std::vector<uint8_t> afterPhasePreserved{
        sequencer::midi::SystemRealTimeMessage::START,
        sequencer::midi::SystemRealTimeMessage::TIMING_CLOCK,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_ON),
        60,
        100,
        sequencer::midi::SystemRealTimeMessage::TIMING_CLOCK,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_OFF),
        60,
        0,
        sequencer::midi::SystemRealTimeMessage::TIMING_CLOCK,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_ON),
        67,
        20,
    };
    if (!expectBytes(recording.record(), afterPhasePreserved, bpm,
                     "ideal tick after late service")) {
        return false;
    }

    // Jump four periods past the third tick's deadline. The task-5.1 Scheduler
    // drains every due tick in one service() call UP TO its documented work
    // budget (kDefaultTickBudget == 8), instead of the old one-tick-per-update
    // behavior. Jumping 4 * floor(tickDuration) past deadline(3) makes either
    // three or four further ticks overdue — the exact count depends on the
    // sub-microsecond remainder the timeline carries at BPMs that do not divide
    // 60e6/(bpm*24) evenly (BPM 40 is exact -> 4 catch-up ticks; 120/255 carry a
    // remainder -> 3). All are well within the budget, so one update() services
    // them all from their intended deadlines — bounded catch-up, not a burst.
    // Each serviced tick emits one Clock plus its gate-edge note. We therefore
    // assert tolerant bounds and strong internal self-consistency rather than a
    // single brittle count.
    clock.setMicros(deadline(3) + 4 * tickDurationUs);
    seq.update();

    const sequencer::PerformanceMetrics& metrics = seq.performanceMetrics();
    // The first (most overdue) catch-up tick is tick 4; its lateness is measured
    // against tick 4's exact deadline and is the maximum this call.
    const uint64_t expectedLatenessApprox =
        (deadline(3) + 4 * tickDurationUs) - deadline(4);
    const bool latenessOk =
        metrics.maxSchedulerLatenessUs >= (expectedLatenessApprox >= 2 ? expectedLatenessApprox - 2 : 0) &&
        metrics.maxSchedulerLatenessUs <= expectedLatenessApprox + 2;
    // 3 earlier ticks + {3 or 4} catch-up ticks = 6 or 7 total; each catch-up
    // tick adds exactly 4 bytes (Clock + note triple) to the 13 bytes through
    // tick 3, so midi is 25 or 29 and MUST equal the recorded stream length.
    const uint64_t ticks = metrics.logicalTicksServiced;
    const bool ticksOk = (ticks == 6 || ticks == 7);
    const uint64_t expectedMidi = 13 + 4 * (ticks - 3);
    const bool metricsMatch =
        ticksOk &&
        latenessOk &&
        (metrics.missedLogicalTicks >= 3 && metrics.missedLogicalTicks <= 6) &&
        metrics.reconciliationCount == 0 &&
        metrics.midiBytesDispatched == recording.record().size() &&
        metrics.midiBytesDispatched == expectedMidi &&
        metrics.operationsApplied == 3 &&
        metrics.midiQueueHighWater == 0 &&
        metrics.commandQueueHighWater == 0 &&
        metrics.lastFault == sequencer::PerformanceFaultCause::NONE;

    if (!metricsMatch) {
        std::fprintf(stderr,
                     "FAIL metrics for BPM %u: ticks=%llu maxLate=%llu missed=%llu "
                     "midi=%llu operations=%llu\n",
                     static_cast<unsigned>(bpm),
                     static_cast<unsigned long long>(metrics.logicalTicksServiced),
                     static_cast<unsigned long long>(metrics.maxSchedulerLatenessUs),
                     static_cast<unsigned long long>(metrics.missedLogicalTicks),
                     static_cast<unsigned long long>(metrics.midiBytesDispatched),
                     static_cast<unsigned long long>(metrics.operationsApplied));
        return false;
    }

    seq.resetPerformanceMetrics();
    const sequencer::PerformanceMetrics& reset = seq.performanceMetrics();
    return reset.logicalTicksServiced == 0 &&
           reset.maxSchedulerLatenessUs == 0 &&
           reset.missedLogicalTicks == 0 &&
           reset.midiBytesDispatched == 0 &&
           reset.operationsApplied == 0 &&
           reset.lastFault == sequencer::PerformanceFaultCause::NONE;
}

}  // namespace

int main() {
    for (const uint16_t bpm : std::array<uint16_t, 3>{40, 120, 255}) {
        if (!verifyExactTickBoundary(bpm)) return 1;
    }

    std::printf(
        "PASS: SDK-free sequencer uses injected 64-bit monotonic time at exact "
        "tick boundaries\n");
    return 0;
}
