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

    const uint64_t tickDurationUs = 60ULL * 1000ULL * 1000ULL / (bpm * 24ULL);
    if (!expectBytes(recording.record(), {sequencer::midi::SystemRealTimeMessage::START},
                     bpm, "play")) {
        return false;
    }

    clock.advanceMicros(tickDurationUs - 1);
    seq.update();
    if (!expectBytes(recording.record(), {sequencer::midi::SystemRealTimeMessage::START},
                     bpm, "one microsecond before first tick")) {
        return false;
    }

    clock.advanceMicros(1);
    seq.update();
    const std::vector<uint8_t> afterRising{
        sequencer::midi::SystemRealTimeMessage::START,
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

    // Service the second tick deliberately late. Current behavior still processes
    // exactly one tick, and advances from the ideal deadline rather than rebasing
    // the phase to the late observation time.
    constexpr uint64_t lateByUs = 500;
    clock.advanceMicros(tickDurationUs + lateByUs);
    seq.update();
    const std::vector<uint8_t> afterFalling{
        sequencer::midi::SystemRealTimeMessage::START,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_ON),
        60,
        100,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_OFF),
        60,
        0,
    };
    if (!expectBytes(recording.record(), afterFalling, bpm, "late second tick")) {
        return false;
    }

    // Only the remainder to the next ideal deadline is needed. If the sequencer
    // had rebased to the late service time, this update would be too early.
    clock.advanceMicros(tickDurationUs - lateByUs);
    seq.update();
    const std::vector<uint8_t> afterPhasePreserved{
        sequencer::midi::SystemRealTimeMessage::START,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_ON),
        60,
        100,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_OFF),
        60,
        0,
        static_cast<uint8_t>(sequencer::midi::ChannelVoiceMessage::NOTE_ON),
        67,
        20,
    };
    return expectBytes(recording.record(), afterPhasePreserved, bpm,
                       "ideal tick after late service");
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
