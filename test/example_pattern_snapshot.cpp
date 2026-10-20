// performance-safe-foundation task 6.4 — fixed-capacity Pattern snapshot.
//
// Validates Requirements 7.4, 7.9, 7.10 against sequencer::PatternSnapshot: the
// allocation-free container core1 decodes a command's gate/pitch/velocity
// material INTO (reused in place) so the receive->apply path performs no
// steady-state heap allocation and keeps a documented fixed memory footprint.
//
//   - gate bitset round-trips every step; counts clamp at the protocol caps;
//   - pitch/velocity arrays round-trip and clamp; data bytes masked to 7 bits;
//   - clear() resets the reused buffer so one instance serves many commands with
//     no reallocation;
//   - footprintBytes() is a compile-time constant (bounded handoff budget).
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "PatternSnapshot.h"
#include "../common/PatternId.h"
#include "../common/pitch_set.h"

#include <cstdint>
#include <cstdio>

using sequencer::PatternSnapshot;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// Gate bitset round-trips; out-of-range reads are false.
void test_gate_bitset() {
    PatternSnapshot s;
    s.setGateCount(100);
    check(s.gateCount() == 100, "gate count stored");
    for (uint16_t i = 0; i < 100; ++i) s.setGate(i, (i % 3) == 0);
    bool okAll = true;
    for (uint16_t i = 0; i < 100; ++i) okAll &= (s.gate(i) == ((i % 3) == 0));
    check(okAll, "every gate step round-trips through the packed bitset");
    check(!s.gate(100) && !s.gate(5000), "out-of-count gate reads are false");
}

// Counts clamp at the protocol caps (no overflow, no growth).
void test_count_clamping() {
    PatternSnapshot s;
    s.setGateCount(10000);  // > kMaxGateSteps (6144), within uint16_t range
    check(s.gateCount() == PatternSnapshot::kMaxGateSteps, "gate count clamps to cap");
    s.setPitchCount(200);   // > kMaxSetLen
    check(s.pitchCount() == PatternSnapshot::kMaxSetLen, "pitch count clamps to cap");
    s.setVelocityCount(200);
    check(s.velocityCount() == PatternSnapshot::kMaxSetLen, "velocity count clamps to cap");
}

// Pitch/velocity round-trip, 7-bit masked, with order.
void test_pitch_velocity() {
    PatternSnapshot s;
    s.setPitchCount(4);
    s.setPitchOrder(common::PlayingOrder::PENDULUM);
    const uint8_t notes[4] = {60, 200 /*->72 masked*/, 64, 127};
    for (uint8_t i = 0; i < 4; ++i) s.setPitch(i, notes[i]);
    check(s.pitch(0) == 60 && s.pitch(2) == 64 && s.pitch(3) == 127, "pitches round-trip");
    check(s.pitch(1) == (200 & 0x7F), "pitch byte masked to 7 bits");
    check(s.pitchOrder() == common::PlayingOrder::PENDULUM, "pitch order stored");
    check(s.pitch(4) == 0, "out-of-count pitch read is 0");

    s.setVelocityCount(2);
    s.setVelocity(0, 100); s.setVelocity(1, 20);
    check(s.velocity(0) == 100 && s.velocity(1) == 20, "velocities round-trip");
}

// clear() resets the reused buffer: one instance serves many commands with no
// reallocation (the allocation-free discipline, Req 7.9).
void test_clear_reuse() {
    PatternSnapshot s;
    s.target = common::PatternId(2, 5);
    s.setGateCount(10);
    for (uint16_t i = 0; i < 10; ++i) s.setGate(i, true);
    s.setPitchCount(3);
    s.clear();
    check(s.gateCount() == 0 && s.pitchCount() == 0 && s.velocityCount() == 0,
          "clear resets all counts");
    check(!s.gate(0), "clear resets the gate bitset");
    check(!s.target.valid(), "clear resets the target id");
    // Reusable immediately for the next decoded command.
    s.setGateCount(2); s.setGate(0, true); s.setGate(1, false);
    check(s.gate(0) && !s.gate(1), "snapshot reusable after clear");
}

// Documented fixed footprint is a compile-time constant.
void test_footprint() {
    static_assert(PatternSnapshot::footprintBytes() > 0, "footprint is a compile-time constant");
    check(PatternSnapshot::footprintBytes() ==
              PatternSnapshot::kGateWords * 4u + PatternSnapshot::kMaxSetLen * 2u + 16u,
          "footprint matches the documented bound");
}

}  // namespace

int main() {
    test_gate_bitset();
    test_count_clamping();
    test_pitch_velocity();
    test_clear_reuse();
    test_footprint();

    if (failures == 0) {
        std::printf("example_pattern_snapshot: all checks passed\n");
        return 0;
    }
    std::printf("example_pattern_snapshot: %d check(s) failed\n", failures);
    return 1;
}
