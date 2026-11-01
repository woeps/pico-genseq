// generative-engine-expansion task 3.3 — emission-lens default-equivalence.
//
// Validates design invariant 1 (Requirements 1.5, 2.3, 3.3, 6.1): a Pattern
// whose Generation Parameters are at their behavior-preserving defaults emits
// EXACTLY the pre-milestone note stream. The lens (Sequencer::emitStep)
// short-circuits on GenerationParameters::isDefault() to the original
// noteLifecycle.noteOn(stored pitch, stored velocity) call, so routing every
// onset through the lens changes nothing when no feature is enabled.
//
// This drives the REAL Sequencer through its public command API with a
// FakeMonotonicClock (the same harness shape as example_sequencer_integration),
// so it exercises the actual serviceTick -> emitStep path rather than a mock.
// The existing property2_sequencer_equivalence / property5_midi_stream suites
// are the broader randomized guard; this is the focused, named assertion the
// task calls for.
//
// Standalone example (own main(), no RapidCheck); excluded from the glob.

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "sequencer.h"
#include "command.h"
#include "midi_messages.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace midi = sequencer::midi;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m; m.cmd = c; m.param1 = p1; return m;
}

// Collect the note numbers of every NOTE_ON (velocity > 0) in order.
std::vector<uint8_t> noteOnPitches(const std::vector<uint8_t>& s) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 2 < s.size() + 1 && i + 2 <= s.size(); ++i) {
        if (i + 2 < s.size() + 1 && (s[i] & 0xF0) == midi::ChannelVoiceMessage::NOTE_ON) {
            // NOTE_ON with velocity 0 is a note-off in running status; count only vel>0.
            if (i + 2 < s.size() && s[i + 2] > 0) {
                out.push_back(s[i + 1]);
                i += 2;
            }
        }
    }
    return out;
}

struct Harness {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};
    uint16_t bpm = 120;
    uint64_t base = 0;

    void setGate(const std::vector<bool>& g) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_GATE_SET, 0);
        m.gateCount = static_cast<uint16_t>(g.size());
        m.gates = g;
        seq.processCommand(m);
    }
    void setPitch(const std::vector<uint8_t>& pitches) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_PITCH_SET, 0);
        m.pitchCount = static_cast<uint8_t>(pitches.size());
        m.pitchOrder = common::PlayingOrder::FORWARDS;
        m.pitches = pitches;
        seq.processCommand(m);
    }
    void setVelocity(const std::vector<uint8_t>& vels) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_VELOCITY_SET, 0);
        m.velocityCount = static_cast<uint8_t>(vels.size());
        m.velocityOrder = common::PlayingOrder::FORWARDS;
        m.velocities = vels;
        seq.processCommand(m);
    }
    void play() { base = clock.nowMicros(); seq.processCommand(cmd(commands::Command::PLAY)); }
    uint64_t deadline(uint64_t n) const {
        return base + (n * 60ULL * 1000000ULL) / (bpm * 24ULL);
    }
    void tick(uint64_t n) { clock.setMicros(deadline(n)); seq.update(); }
};

// With default Generation Parameters, a forward pitch set emits its stored
// pitches in order, unchanged — the lens is transparent.
void test_default_emits_stored_pitches() {
    Harness h;
    h.setPitch({60, 64, 67});                       // C E G
    h.setVelocity({100});
    h.setGate({true, false, true, false, true, false});  // 3 onsets
    h.play();
    for (uint64_t n = 1; n <= 6; ++n) h.tick(n);

    const std::vector<uint8_t> notes = noteOnPitches(h.out.record());
    check(notes.size() == 3, "three note-ons for three gated onsets");
    if (notes.size() == 3) {
        check(notes[0] == 60 && notes[1] == 64 && notes[2] == 67,
              "default lens emits stored pitches 60/64/67 unchanged");
    }
}

// With default Generation Parameters, the stored velocity is emitted unchanged
// (no humanization).
void test_default_emits_stored_velocity() {
    Harness h;
    h.setPitch({60});
    h.setVelocity({77});
    h.setGate({true, false});
    h.play();
    h.tick(1);
    const std::vector<uint8_t>& s = h.out.record();
    // Find the NOTE_ON and read its velocity byte.
    int vel = -1;
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if ((s[i] & 0xF0) == midi::ChannelVoiceMessage::NOTE_ON && s[i + 2] > 0) {
            vel = s[i + 2]; break;
        }
    }
    check(vel == 77, "default lens emits stored velocity 77 unchanged");
}

}  // namespace

int main() {
    test_default_emits_stored_pitches();
    test_default_emits_stored_velocity();

    if (failures == 0) {
        std::printf("example_emission_lens: all checks passed\n");
        return 0;
    }
    std::printf("example_emission_lens: %d check(s) failed\n", failures);
    return 1;
}
