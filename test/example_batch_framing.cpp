// separate-velocity-set-gate-set-and-note-set task 3.3 -- core1 command batch framing (host).
//
// BATCH_BEGIN(n) holds the tick path until n commands have been applied (or BATCH_END arrives),
// so a set edit fanned out to several patterns is never observed half applied (R8.2). A batch
// that stalls for more than one tick period is abandoned so the clock cannot freeze, and is
// counted in PerformanceMetrics::batchTimeouts. Standalone example (own main()).

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

int countByte(const std::vector<uint8_t>& s, uint8_t b) {
    int n = 0;
    for (uint8_t x : s) if (x == b) ++n;
    return n;
}

int countNoteOn(const std::vector<uint8_t>& s, uint8_t note) {
    int n = 0;
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if ((s[i] & 0xF0) == midi::ChannelVoiceMessage::NOTE_ON && s[i + 1] == note && s[i + 2] > 0) ++n;
    }
    return n;
}

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m;
    m.cmd = c;
    m.param1 = p1;
    return m;
}

struct Harness {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};
    uint64_t base = 0;

    uint64_t deadline(uint64_t n) const { return base + (n * 60ULL * 1000000ULL) / (120ULL * 24ULL); }
    void tick(uint64_t n) { clock.setMicros(deadline(n)); seq.update(); }
    void send(commands::Command c, uint8_t p1 = 0) { seq.processCommand(cmd(c, p1)); }
    void pitch(uint8_t p, uint8_t note) {
        auto m = cmd(commands::Command::PATTERN_PITCH_SET, p);
        m.pitchCount = 1;
        m.pitchOrder = common::PlayingOrder::FORWARDS;
        m.pitches = {note};
        seq.processCommand(m);
    }
    void gate(uint8_t p, const std::vector<bool>& g) {
        auto m = cmd(commands::Command::PATTERN_GATE_SET, p);
        m.gateCount = static_cast<uint16_t>(g.size());
        m.gates = g;
        seq.processCommand(m);
    }
    // Two patterns, both gate {on,off}, pitch 60, then Play.
    void setupTwoPatterns() {
        send(commands::Command::PATTERN_ADD);
        for (uint8_t p = 0; p < 2; ++p) { gate(p, {true, false}); pitch(p, 60); }
        base = clock.nowMicros();
        send(commands::Command::PLAY);
    }
};

void test_ticks_held_until_batch_ends() {
    Harness h;
    h.setupTwoPatterns();
    h.out.clear();

    h.send(commands::Command::BATCH_BEGIN, 2);
    h.tick(1);  // due, but the batch is open
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) == 0,
          "no tick is serviced while a batch is open");

    h.pitch(0, 72);
    h.tick(1);
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) == 0,
          "still held after 1 of 2 batch commands");
    h.send(commands::Command::BATCH_END);
    h.tick(1);
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) >= 1,
          "tick resumes after BATCH_END");
    check(h.seq.performanceMetrics().batchTimeouts == 0, "no timeout on a clean batch");
}

void test_batch_closes_when_count_reached() {
    Harness h;
    h.setupTwoPatterns();
    h.out.clear();

    h.send(commands::Command::BATCH_BEGIN, 2);
    h.pitch(0, 72);
    h.pitch(1, 72);  // second command completes the batch without BATCH_END
    h.tick(1);
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) >= 1,
          "tick resumes once the declared command count was applied");
    check(h.seq.performanceMetrics().batchTimeouts == 0, "count-closed batch is not a timeout");
}

void test_no_tick_sees_mixed_material() {
    Harness h;
    h.setupTwoPatterns();
    h.out.clear();

    h.send(commands::Command::BATCH_BEGIN, 2);
    h.pitch(0, 72);
    h.tick(1);          // would fire both onsets; must be held while pattern 1 is still old
    h.pitch(1, 72);
    h.tick(1);
    check(countNoteOn(h.out.record(), 60) == 0, "no onset plays the old pitch from either pattern");
    check(countNoteOn(h.out.record(), 72) == 2, "both patterns sound the new pitch together");
}

void test_stalled_batch_times_out() {
    Harness h;
    h.setupTwoPatterns();
    h.out.clear();

    h.send(commands::Command::BATCH_BEGIN, 3);
    h.pitch(0, 72);  // two more commands never arrive
    h.tick(1);
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) == 0, "held at first");
    // Move past one tick period (20833 us at 120 BPM) since the last batch activity.
    h.clock.setMicros(h.deadline(1) + 25000);
    h.seq.update();
    check(h.seq.performanceMetrics().batchTimeouts == 1, "stalled batch is counted as a timeout");
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) >= 1,
          "ticking resumes after the timeout");
}

void test_unframed_commands_unchanged() {
    Harness h;
    h.setupTwoPatterns();
    h.out.clear();
    h.pitch(0, 72);
    h.tick(1);
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) >= 1,
          "a single unframed command does not hold ticks");
    check(countNoteOn(h.out.record(), 72) == 1 && countNoteOn(h.out.record(), 60) == 1,
          "unframed edits apply immediately per pattern");
}

void test_idle_noop_does_not_consume_batch() {
    Harness h;
    h.setupTwoPatterns();
    h.out.clear();
    h.send(commands::Command::BATCH_BEGIN, 1);
    h.send(commands::Command::NOOP);  // idle loop iterations arrive as NOOP
    h.send(commands::Command::NOOP);
    h.tick(1);
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) == 0,
          "NOOP does not count against the batch");
}

}  // namespace

int main() {
    test_ticks_held_until_batch_ends();
    test_batch_closes_when_count_reached();
    test_no_tick_sees_mixed_material();
    test_stalled_batch_times_out();
    test_unframed_commands_unchanged();
    test_idle_noop_does_not_consume_batch();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("batch framing OK\n");
    return 0;
}
