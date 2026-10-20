// performance-safe-foundation task 5.4 — fake-clock scheduler + fault tests.
//
// Validates Requirements 3.1-3.8, 10.1-10.7, 11.1-11.3 by driving the REAL
// Sequencer through its public API with a FakeMonotonicClock across the three
// lateness regimes:
//   - just UNDER one beat: ticks are reconciled (phase advances, crossed
//     releases emitted) with NO catch-up burst of obsolete Note On / Clock bytes
//     (Req 3.4-3.6); playback continues (no fault).
//   - AT / BEYOND one beat: the Sequencer fails closed — it stops, releases all
//     notes, emits one Stop (clock enabled), latches the fault cause, and does
//     NOT restart automatically (Req 3.7, 10.1-10.5).
//   - explicit Play after a fault acknowledges it and restarts from the
//     beginning (Req 10.6).
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "sequencer.h"
#include "command.h"
#include "midi_messages.h"
#include "PerformanceMetrics.h"

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

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m; m.cmd = c; m.param1 = p1; return m;
}

struct Harness {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};
    uint16_t bpm = 120;
    uint64_t base = 0;

    void setGate(const std::vector<bool>& g) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_GATE_SET, 0);
        m.gateCount = static_cast<uint16_t>(g.size()); m.gates = g;
        seq.processCommand(m);
    }
    void setPitch(const std::vector<uint8_t>& p) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_PITCH_SET, 0);
        m.pitchCount = static_cast<uint8_t>(p.size());
        m.pitchOrder = common::PlayingOrder::FORWARDS; m.pitches = p;
        seq.processCommand(m);
    }
    void play() { base = clock.nowMicros(); seq.processCommand(cmd(commands::Command::PLAY)); }
    uint64_t tickDur() const { return 60ULL * 1000000ULL / (bpm * 24ULL); }
    uint64_t deadline(uint64_t n) const { return base + (n * 60ULL * 1000000ULL) / (bpm * 24ULL); }
    void updateAt(uint64_t micros) { clock.setMicros(micros); seq.update(); }
};

// Under one beat: reconciliation, no catch-up burst, no fault.
void test_under_one_beat_reconciles_no_burst() {
    Harness h;
    h.setPitch({60});
    h.setGate({true, false});  // alternating edges
    h.play();
    // Jump ~10 ticks late (well under one beat = 24 ticks). The scheduler advances
    // all 10 but reconciles the far-overdue ones (no obsolete Note On / Clock);
    // only the authoritative tick emits normally.
    h.updateAt(h.deadline(10) + h.tickDur() / 2);

    const auto& m = h.seq.performanceMetrics();
    check(m.lastFault == sequencer::PerformanceFaultCause::NONE, "no fault under one beat");
    check(m.reconciliationCount > 0, "far-overdue ticks were reconciled");
    // No catch-up BURST: far fewer Clock bytes than serviced ticks (reconciled
    // ticks emit none). A burst would emit one Clock per missed tick.
    const int clocks = countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK);
    check(clocks < static_cast<int>(m.logicalTicksServiced),
          "reconciled ticks suppress Clock bytes -> no catch-up burst (Req 3.6)");
}

// At/beyond one beat: fail closed.
void test_one_beat_faults_closed() {
    Harness h;
    h.setPitch({60});
    h.setGate({true, true});  // note is held so a hanging note is possible
    h.play();
    h.updateAt(h.deadline(1));  // first tick: NoteOn(60), held
    check(countByte(h.out.record(), midi::ChannelVoiceMessage::NOTE_ON) >= 1, "note sounding");

    h.out.clear();
    // Now jump a full beat (24 ticks) + margin past the next due tick -> fault.
    h.updateAt(h.deadline(2) + 24ULL * h.tickDur() + 10);

    const auto& m = h.seq.performanceMetrics();
    check(m.lastFault == sequencer::PerformanceFaultCause::SCHEDULER_LATENESS,
          "lateness of one beat latches SCHEDULER_LATENESS fault (Req 3.7)");
    // Fail-closed cleanup: the held note is released and exactly one Stop emitted.
    check(countByte(h.out.record(), midi::ChannelVoiceMessage::NOTE_OFF) >= 1,
          "fault releases the held note (no hanging note, Req 10.2)");
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::STOP) == 1,
          "fault emits exactly one Stop (clock enabled, Req 10.2)");

    // No automatic restart: a further update() does nothing (playback stopped).
    h.out.clear();
    h.updateAt(h.deadline(2) + 48ULL * h.tickDur());
    check(h.out.record().empty(), "no automatic restart while faulted (Req 10.5)");
}

// Explicit Play after a fault acknowledges and restarts from the beginning.
void test_play_restarts_after_fault() {
    Harness h;
    h.setPitch({60});
    h.setGate({true, false});
    h.play();
    h.updateAt(h.deadline(2) + 24ULL * h.tickDur() + 10);  // force a fault
    check(h.seq.performanceMetrics().lastFault != sequencer::PerformanceFaultCause::NONE,
          "faulted before restart");

    h.out.clear();
    // Explicit Play: acknowledges the fault and restarts transport from the start.
    h.base = h.clock.nowMicros();
    h.seq.processCommand(cmd(commands::Command::PLAY));
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::START) == 1,
          "explicit Play after fault restarts (one Start, Req 10.6)");
    // The restarted timeline services ticks again from the beginning.
    h.out.clear();
    h.updateAt(h.deadline(1));
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) == 1,
          "restarted timeline services ticks from the beginning");
}

}  // namespace

int main() {
    test_under_one_beat_reconciles_no_burst();
    test_one_beat_faults_closed();
    test_play_restarts_after_fault();

    if (failures == 0) {
        std::printf("example_scheduler_fault: all checks passed\n");
        return 0;
    }
    std::printf("example_scheduler_fault: %d check(s) failed\n", failures);
    return 1;
}
