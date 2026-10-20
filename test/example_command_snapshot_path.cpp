// performance-safe-foundation task 6.4 — allocation-free core1 command path.
//
// The firmware core1 loop was cut over from the heap-allocating
// commands::receiveCommand() (which .assign()s std::vector members of
// CommandMessage on every pattern-set command — forbidden steady-state
// allocation, Req 7.9) to commands::receiveCommandInto(snapshot) +
// Sequencer::processCommand(msg, snapshot), which decode the multiword payload
// into a reused fixed-capacity PatternSnapshot.
//
// receiveCommandInto() itself is SDK-only (multicore FIFO), but the APPLICATION
// half — Sequencer::processCommand(msg, snapshot) reading the three pattern-set
// payloads from a PatternSnapshot instead of CommandMessage vectors — is pure
// host logic. This test proves that path is BEHAVIORALLY EQUIVALENT to the
// legacy scalar path: the same gate/pitch/velocity edits applied via the
// snapshot overload produce the identical MIDI stream as the vector overload.
//
// It also asserts the snapshot overload validates set commands against the
// SNAPSHOT's counts (not CommandMessage's now-empty vectors), so a well-formed
// edit is applied and an out-of-range target is rejected.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "sequencer.h"
#include "PatternSnapshot.h"
#include "command.h"
#include "midi_messages.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace midi = sequencer::midi;
using sequencer::PatternSnapshot;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m;
    m.cmd = c;
    m.param1 = p1;
    return m;
}

// Fill a snapshot from plain vectors exactly as receiveCommandInto() would from
// the FIFO payload (the decode is byte-for-byte identical; only the transport
// differs, and the transport is not host-reachable).
PatternSnapshot snapGate(const std::vector<bool>& g) {
    PatternSnapshot s;
    s.setGateCount(static_cast<uint16_t>(g.size()));
    for (uint16_t i = 0; i < g.size(); ++i) s.setGate(i, g[i]);
    return s;
}
PatternSnapshot snapPitch(const std::vector<uint8_t>& p, common::PlayingOrder o) {
    PatternSnapshot s;
    s.setPitchCount(static_cast<uint8_t>(p.size()));
    s.setPitchOrder(o);
    for (uint8_t i = 0; i < p.size(); ++i) s.setPitch(i, p[i]);
    return s;
}
PatternSnapshot snapVel(const std::vector<uint8_t>& v, common::PlayingOrder o) {
    PatternSnapshot s;
    s.setVelocityCount(static_cast<uint8_t>(v.size()));
    s.setVelocityOrder(o);
    for (uint8_t i = 0; i < v.size(); ++i) s.setVelocity(i, v[i]);
    return s;
}

// Drive one sequencer through gate+pitch+velocity edits then two ticks, via the
// SCALAR (vector) command path. Returns the recorded MIDI stream.
std::vector<uint8_t> runScalar() {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};

    commands::CommandMessage g = cmd(commands::Command::PATTERN_GATE_SET, 0);
    g.gateCount = 2; g.gates = {true, false};
    seq.processCommand(g);

    commands::CommandMessage p = cmd(commands::Command::PATTERN_PITCH_SET, 0);
    p.pitchCount = 1; p.pitchOrder = common::PlayingOrder::FORWARDS; p.pitches = {60};
    seq.processCommand(p);

    commands::CommandMessage v = cmd(commands::Command::PATTERN_VELOCITY_SET, 0);
    v.velocityCount = 1; v.velocityOrder = common::PlayingOrder::FORWARDS; v.velocities = {100};
    seq.processCommand(v);

    const uint64_t base = clock.nowMicros();
    seq.processCommand(cmd(commands::Command::PLAY));
    const auto deadline = [&](uint64_t n) { return base + (n * 60ULL * 1000000ULL) / (120 * 24ULL); };
    clock.setMicros(deadline(1)); seq.update();  // rising -> NoteOn(60,100)
    clock.setMicros(deadline(2)); seq.update();  // falling -> NoteOff(60)
    return out.record();
}

// Same edits + ticks via the SNAPSHOT (allocation-free) command path.
std::vector<uint8_t> runSnapshot() {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};

    commands::CommandMessage g = cmd(commands::Command::PATTERN_GATE_SET, 0);
    g.gateCount = 2;  // scalar fields only; vectors stay empty
    PatternSnapshot sg = snapGate({true, false});
    seq.processCommand(g, sg);

    commands::CommandMessage p = cmd(commands::Command::PATTERN_PITCH_SET, 0);
    p.pitchCount = 1; p.pitchOrder = common::PlayingOrder::FORWARDS;
    PatternSnapshot sp = snapPitch({60}, common::PlayingOrder::FORWARDS);
    seq.processCommand(p, sp);

    commands::CommandMessage v = cmd(commands::Command::PATTERN_VELOCITY_SET, 0);
    v.velocityCount = 1; v.velocityOrder = common::PlayingOrder::FORWARDS;
    PatternSnapshot sv = snapVel({100}, common::PlayingOrder::FORWARDS);
    seq.processCommand(v, sv);

    const uint64_t base = clock.nowMicros();
    PatternSnapshot none;
    seq.processCommand(cmd(commands::Command::PLAY), none);
    const auto deadline = [&](uint64_t n) { return base + (n * 60ULL * 1000000ULL) / (120 * 24ULL); };
    clock.setMicros(deadline(1)); seq.update();
    clock.setMicros(deadline(2)); seq.update();
    return out.record();
}

void test_snapshot_path_matches_scalar() {
    const std::vector<uint8_t> scalar = runScalar();
    const std::vector<uint8_t> snapshot = runSnapshot();
    check(!scalar.empty(), "scalar path produced a MIDI stream");
    check(scalar == snapshot,
          "snapshot (allocation-free) path produces the identical MIDI stream as the scalar path");
}

void test_snapshot_validation() {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};

    // Out-of-range target pattern is rejected (not applied) — only pattern 0 exists.
    commands::CommandMessage bad = cmd(commands::Command::PATTERN_GATE_SET, 7);
    bad.gateCount = 2;
    PatternSnapshot sg = snapGate({true, true});
    seq.processCommand(bad, sg);
    check(seq.performanceMetrics().operationsApplied == 0,
          "snapshot path rejects an out-of-range pattern target (no op applied)");

    // Well-formed edit to the live pattern is applied and counted.
    commands::CommandMessage ok = cmd(commands::Command::PATTERN_GATE_SET, 0);
    ok.gateCount = 2;
    seq.processCommand(ok, sg);
    check(seq.performanceMetrics().operationsApplied == 1,
          "snapshot path applies a well-formed set command");
}

}  // namespace

int main() {
    test_snapshot_path_matches_scalar();
    test_snapshot_validation();

    if (failures == 0) {
        std::printf("example_command_snapshot_path: all checks passed\n");
        return 0;
    }
    std::printf("example_command_snapshot_path: %d check(s) failed\n", failures);
    return 1;
}
