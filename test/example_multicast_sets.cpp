// separate-velocity-set-gate-set-and-note-set (10.2 follow-up) -- multicast set commands.
//
// A set shared by several patterns is sent ONCE with a pattern mask instead of once per pattern.
// This test runs the REAL command.cpp encoder/decoder over a std::queue FIFO double and the real
// Sequencer, and checks:
//   - the multicast gate/pitch/velocity commands round-trip (mask, counts, order, payload) through
//     both receive paths, and the FIFO is fully drained (word count matches the encoder);
//   - the single-pattern commands still encode/decode exactly as before;
//   - applying a multicast set gives byte-identical MIDI to applying the same set to each masked
//     pattern with the single command, including mid-play gate edits (per-pattern phase via
//     continueFrom) and unmasked patterns staying untouched;
//   - a mask naming a pattern that does not exist is rejected.
// Own main(), excluded from the glob.

#include <cstdint>
#include <cstdio>
#include <queue>
#include <vector>

static std::queue<uint32_t> g_fifo;

void multicore_fifo_push_blocking(uint32_t v) { g_fifo.push(v); }
uint32_t multicore_fifo_pop_blocking(void) {
    if (g_fifo.empty()) return 0;
    const uint32_t v = g_fifo.front();
    g_fifo.pop();
    return v;
}
bool multicore_fifo_rvalid(void) { return !g_fifo.empty(); }

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "command.h"
#include "PatternSnapshot.h"
#include "sequencer.h"

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

std::vector<bool> makeGates(uint16_t n, uint16_t period) {
    std::vector<bool> g(n, false);
    for (uint16_t i = 0; i < n; ++i) g[i] = (i % period) < period / 2;
    return g;
}

void test_wire_round_trip() {
    using commands::Command;
    sequencer::PatternSnapshot snap;

    const std::vector<bool> gates = makeGates(6144, 7);
    commands::sendGateSetMulti(0x7FFF, gates);
    commands::CommandMessage m = commands::receiveCommandInto(snap);
    check(m.cmd == Command::PATTERN_GATE_SET_MULTI, "gate multicast decodes as GATE_SET_MULTI");
    check(m.patternMask == 0x7FFF, "gate mask round-trips");
    check(m.gateCount == 6144 && snap.gateCount() == 6144, "gate count round-trips (max length)");
    bool same = true;
    for (uint16_t i = 0; i < 6144; ++i) same &= snap.gate(i) == gates[i];
    check(same, "every gate bit round-trips");
    check(g_fifo.empty(), "gate multicast leaves the FIFO drained");

    commands::sendPitchSetMulti(0x0005, 3, common::PlayingOrder::PENDULUM, {60, 64, 67});
    m = commands::receiveCommandInto(snap);
    check(m.cmd == Command::PATTERN_PITCH_SET_MULTI && m.patternMask == 0x0005, "pitch multicast header");
    check(snap.pitchCount() == 3 && snap.pitch(0) == 60 && snap.pitch(2) == 67 &&
              snap.pitchOrder() == common::PlayingOrder::PENDULUM,
          "pitch payload round-trips");
    check(g_fifo.empty(), "pitch multicast leaves the FIFO drained");

    commands::sendVelocitySetMulti(0x8001, 5, common::PlayingOrder::RANDOM, {1, 2, 3, 4, 127});
    m = commands::receiveCommand();  // heap path decodes the same wire format
    check(m.cmd == Command::PATTERN_VELOCITY_SET_MULTI && m.patternMask == 0x8001,
          "velocity multicast header (heap receive path)");
    check(m.velocityCount == 5 && m.velocities.size() == 5 && m.velocities[4] == 127 &&
              m.velocityOrder == common::PlayingOrder::RANDOM,
          "velocity payload round-trips (heap receive path)");
    check(g_fifo.empty(), "velocity multicast leaves the FIFO drained");

    // Single-pattern commands are unchanged: same header word, no mask word.
    commands::sendGateSet(4, makeGates(96, 6));
    check(g_fifo.size() == 1 + 3, "single gate command is header + payload words only");
    m = commands::receiveCommandInto(snap);
    check(m.cmd == Command::PATTERN_GATE_SET && m.param1 == 4 && m.patternMask == 0,
          "single gate command still decodes with its pattern index");
    commands::sendPitchSet(2, 1, common::PlayingOrder::FORWARDS, {72});
    check(g_fifo.size() == 1 + 1, "single pitch command is header + one payload word");
    m = commands::receiveCommandInto(snap);
    check(m.cmd == Command::PATTERN_PITCH_SET && m.param1 == 2 && snap.pitch(0) == 72,
          "single pitch command still decodes");
    check(g_fifo.empty(), "FIFO drained after the single commands");
}

// Drive a Sequencer through the FIFO exactly like the core1 loop: receiveCommandInto, then the
// snapshot apply path.
struct Core1 {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};
    sequencer::PatternSnapshot snap;
    uint64_t base = 0;

    void drain() {
        while (!g_fifo.empty()) {
            const commands::CommandMessage m = commands::receiveCommandInto(snap);
            seq.processCommand(m, snap);
        }
    }
    void tick(uint64_t n) {
        clock.setMicros(base + (n * 60ULL * 1000000ULL) / (120ULL * 24ULL));
        seq.update();
    }
    void addPatterns(uint8_t n) {
        for (uint8_t i = 1; i < n; ++i) commands::sendCommand(commands::Command::PATTERN_ADD);
        drain();
        seq.applyInstrument(0, 0, static_cast<uint16_t>((1u << n) - 1u));  // all on channel 1
    }
    void play() {
        base = clock.nowMicros();
        commands::sendCommand(commands::Command::PLAY);
        drain();
    }
};

// Patterns 0..3; pattern 3 is never in the mask. Every pattern starts with its own material.
void seed(Core1& c) {
    c.addPatterns(4);
    for (uint8_t p = 0; p < 4; ++p) {
        commands::sendGateSet(p, makeGates(48, static_cast<uint16_t>(4 + 2 * p)));
        commands::sendPitchSet(p, 2, common::PlayingOrder::FORWARDS,
                               {static_cast<uint8_t>(40 + p), static_cast<uint8_t>(50 + p)});
        commands::sendVelocitySet(p, 1, common::PlayingOrder::FORWARDS, {static_cast<uint8_t>(90 + p)});
    }
    c.drain();
}

// Mid-play: replace the material of patterns 0..2 at tick 37 (patterns are at different phases),
// either with one multicast per set type or with three single commands per type.
std::vector<uint8_t> runEdit(bool multicast) {
    Core1 c;
    seed(c);
    c.play();
    for (uint64_t n = 1; n <= 36; ++n) c.tick(n);
    const std::vector<bool> gates = makeGates(60, 10);
    const std::vector<uint8_t> pitches = {70, 72, 74};
    const std::vector<uint8_t> velocities = {100, 60};
    if (multicast) {
        commands::sendGateSetMulti(0b0111, gates);
        commands::sendPitchSetMulti(0b0111, 3, common::PlayingOrder::PENDULUM, pitches);
        commands::sendVelocitySetMulti(0b0111, 2, common::PlayingOrder::BACKWARDS, velocities);
    } else {
        for (uint8_t p = 0; p < 3; ++p) {
            commands::sendGateSet(p, gates);
            commands::sendPitchSet(p, 3, common::PlayingOrder::PENDULUM, pitches);
            commands::sendVelocitySet(p, 2, common::PlayingOrder::BACKWARDS, velocities);
        }
    }
    c.drain();
    for (uint64_t n = 37; n <= 240; ++n) c.tick(n);
    return c.out.record();
}

int countNoteOn(const std::vector<uint8_t>& s, int note) {
    int n = 0;
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if ((s[i] & 0xF0) == 0x90 && s[i + 2] > 0 && s[i + 1] == note) ++n;
    }
    return n;
}

void test_multicast_equals_per_pattern_singles() {
    const std::vector<uint8_t> multi = runEdit(true);
    const std::vector<uint8_t> single = runEdit(false);
    check(!multi.empty(), "patterns play");
    check(multi == single, "multicast apply == the same set sent to each pattern (mid-play)");
    check(countNoteOn(multi, 70) > 0, "masked patterns play the new pitches");
    check(countNoteOn(multi, 43) > 0 || countNoteOn(multi, 53) > 0,
          "unmasked pattern 3 keeps its own pitches");
}

void test_mask_beyond_pattern_count_rejected() {
    Core1 c;
    seed(c);  // 4 patterns
    commands::sendPitchSetMulti(0b10001, 1, common::PlayingOrder::FORWARDS, {99});  // names pattern 4
    c.drain();
    c.play();
    for (uint64_t n = 1; n <= 96; ++n) c.tick(n);
    check(countNoteOn(c.out.record(), 99) == 0, "a mask naming a missing pattern is rejected whole");
}

}  // namespace

int main() {
    test_wire_round_trip();
    test_multicast_equals_per_pattern_singles();
    test_mask_beyond_pattern_count_rejected();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("multicast sets OK\n");
    return 0;
}
