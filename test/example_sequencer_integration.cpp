// performance-safe-foundation tasks 2.4, 2.5, 3.3 (and 4.1) — Sequencer integration.
//
// Drives the real Sequencer through its public API with a FakeMonotonicClock to validate the
// wave-3 wiring of NoteLifecycle + TransportTimeline into the hot path:
//   - 2.4: a falling gate edge releases the RECORDED onset note even after the pattern's pitch
//     set was replaced mid-note; Stop releases everything; a repeated Play is a deterministic
//     no-op (no second Start, ownership intact).
//   - 2.5: muting (deactivate) and removing a pattern release its owned note at the boundary.
//   - 3.3 / 4.1: Play emits one Start, each serviced tick emits one Clock (PPQN 24 -> 1/tick),
//     Stop emits one Stop; disabling via a fresh default is covered by the unit tests.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

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

// Count occurrences of a status byte in a stream.
int countByte(const std::vector<uint8_t>& s, uint8_t b) {
    int n = 0;
    for (uint8_t x : s) if (x == b) ++n;
    return n;
}

// Find the first NOTE_OFF triple and return its note number (-1 if none).
int firstNoteOffNote(const std::vector<uint8_t>& s) {
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if ((s[i] & 0xF0) == midi::ChannelVoiceMessage::NOTE_OFF) return s[i + 1];
    }
    return -1;
}
int firstNoteOnNote(const std::vector<uint8_t>& s) {
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if ((s[i] & 0xF0) == midi::ChannelVoiceMessage::NOTE_ON) return s[i + 1];
    }
    return -1;
}

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m;
    m.cmd = c;
    m.param1 = p1;
    return m;
}

// Advance the clock to the exact deadline of logical tick `n` (base captured at play) and
// service one tick.
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
    void play() { base = clock.nowMicros(); seq.processCommand(cmd(commands::Command::PLAY)); }
    uint64_t deadline(uint64_t n) const {
        return base + (n * 60ULL * 1000000ULL) / (bpm * 24ULL);
    }
    void tick(uint64_t n) { clock.setMicros(deadline(n)); seq.update(); }
};

// 2.4: falling edge releases the RECORDED onset note, not an edited pitch.
void test_falling_releases_recorded_onset() {
    Harness h;
    h.setPitch({60});          // single pitch so onset is deterministically 60
    h.setGate({true, false});  // rising then falling
    h.play();
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::START) == 1, "one Start on play");

    h.tick(1);  // rising -> NoteOn(60)
    check(firstNoteOnNote(h.out.record()) == 60, "onset recorded at pitch 60");

    // Edit the pitch set mid-note to a different value BEFORE the falling edge.
    h.setPitch({72});

    h.out.clear();
    h.tick(2);  // falling -> must release the RECORDED 60, not the edited 72
    check(firstNoteOffNote(h.out.record()) == 60,
          "falling edge releases recorded onset (60), not edited pitch (72)");
}

// 4.1: one Clock byte per serviced tick; Stop emits exactly one Stop.
void test_clock_and_stop() {
    Harness h;
    h.setPitch({60});
    h.setGate({true, false, true, false});
    h.play();
    for (uint64_t n = 1; n <= 4; ++n) h.tick(n);
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) == 4,
          "one Clock per serviced tick (4 ticks)");

    h.out.clear();
    h.seq.processCommand(cmd(commands::Command::STOP));
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::STOP) == 1, "one Stop on stop");
}

// 2.4: Stop releases a sounding note (no hanging note after stop).
void test_stop_releases_notes() {
    Harness h;
    h.setPitch({60});
    h.setGate({true, true});  // stays HIGH, note sounds and is held
    h.play();
    h.tick(1);  // rising -> NoteOn(60), stays held (next is HIGH)
    h.out.clear();
    h.seq.processCommand(cmd(commands::Command::STOP));
    check(firstNoteOffNote(h.out.record()) == 60, "Stop releases the held note");
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::STOP) == 1, "Stop byte emitted");
}

// 5.10: a repeated Play while already playing emits no second Start and does not drop the note.
void test_repeated_play_noop() {
    Harness h;
    h.setPitch({60});
    h.setGate({true, true});
    h.play();
    h.tick(1);  // note sounding
    h.out.clear();
    h.play();  // repeated Play
    check(h.out.record().empty(), "repeated Play emits nothing (no second Start, no release)");
}

// 2.5: muting (deactivate) releases the pattern's owned note at the boundary.
void test_mute_releases() {
    Harness h;
    h.setPitch({60});
    h.setGate({true, true});
    h.play();
    h.tick(1);  // NoteOn(60) held
    h.out.clear();
    h.seq.processCommand(cmd(commands::Command::PATTERN_DEACTIVATE, 0));
    check(firstNoteOffNote(h.out.record()) == 60, "deactivate (mute) releases the owned note");
}

// 2.5: removing a pattern releases its owned note before destroying runtime state.
void test_remove_releases() {
    Harness h;
    h.setPitch({60});
    h.setGate({true, true});
    // Add a second pattern so removal of index 0 is allowed (size > 1).
    h.seq.processCommand(cmd(commands::Command::PATTERN_ADD));
    h.play();
    h.tick(1);  // pattern 0 NoteOn(60) held
    h.out.clear();
    h.seq.processCommand(cmd(commands::Command::PATTERN_REMOVE, 0));
    check(firstNoteOffNote(h.out.record()) == 60, "remove releases the owned note");
}

}  // namespace

int main() {
    test_falling_releases_recorded_onset();
    test_clock_and_stop();
    test_stop_releases_notes();
    test_repeated_play_noop();
    test_mute_releases();
    test_remove_releases();

    if (failures == 0) {
        std::printf("example_sequencer_integration: all checks passed\n");
        return 0;
    }
    std::printf("example_sequencer_integration: %d check(s) failed\n", failures);
    return 1;
}
