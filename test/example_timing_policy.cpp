// performance-safe-foundation task 7.1 — TimingPolicy + effective-tick calc.
//
// Validates Requirements 6.1, 6.8 against sequencer::timing_policy: Immediate,
// Next Beat, and Next Bar resolve to the correct absolute logical tick against
// the Transport Timeline, with zero boundary error and the deterministic
// "on a boundary -> next boundary" convention.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "TimingPolicy.h"
#include "TransportTimeline.h"

#include <cstdint>
#include <cstdio>

using sequencer::TimingPolicy;
using sequencer::TransportTimeline;
namespace tp = sequencer::timing_policy;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// 4/4 at PPQN 24: beat = 24 ticks, bar = 96 ticks.
void test_immediate() {
    TransportTimeline tl;
    tl.setBpm(120);
    tl.start(0);
    for (int i = 0; i < 5; ++i) tl.advance();  // tick index 5
    check(tp::effectiveTick(TimingPolicy::Immediate, tl) == 5,
          "Immediate resolves to the current tick index");
}

void test_next_beat() {
    TransportTimeline tl;
    tl.setBpm(120);
    tl.start(0);
    check(tl.ticksPerBeat() == 24, "4/4 beat = 24 ticks");
    // At tick 0 (on a beat boundary) -> next beat is tick 24, not 0.
    check(tp::effectiveTick(TimingPolicy::NextBeat, tl) == 24,
          "on a beat boundary resolves to the NEXT beat (deterministic)");
    for (int i = 0; i < 5; ++i) tl.advance();  // tick 5
    check(tp::effectiveTick(TimingPolicy::NextBeat, tl) == 24, "mid-beat -> end of this beat (24)");
    for (int i = 0; i < 20; ++i) tl.advance();  // tick 25
    check(tp::effectiveTick(TimingPolicy::NextBeat, tl) == 48, "tick 25 -> next beat 48");
}

void test_next_bar() {
    TransportTimeline tl;
    tl.setBpm(120);
    tl.start(0);
    check(tl.ticksPerBar() == 96, "4/4 bar = 96 ticks");
    check(tp::effectiveTick(TimingPolicy::NextBar, tl) == 96,
          "on a bar boundary resolves to the NEXT bar");
    for (int i = 0; i < 50; ++i) tl.advance();  // tick 50
    check(tp::effectiveTick(TimingPolicy::NextBar, tl) == 96, "mid-bar -> end of this bar (96)");
    for (int i = 0; i < 50; ++i) tl.advance();  // tick 100
    check(tp::effectiveTick(TimingPolicy::NextBar, tl) == 192, "tick 100 -> next bar 192");
}

// Different meter: 3/8 -> beat = 12 ticks, bar = 36 ticks.
void test_meter_3_8() {
    TransportTimeline tl;
    tl.setBpm(120);
    tl.setTimeSignature(common::TimeSignature(3, 8));
    tl.start(0);
    check(tl.ticksPerBeat() == 12 && tl.ticksPerBar() == 36, "3/8 -> beat 12, bar 36");
    for (int i = 0; i < 5; ++i) tl.advance();  // tick 5
    check(tp::effectiveTick(TimingPolicy::NextBeat, tl) == 12, "3/8 next beat from tick 5 is 12");
    check(tp::effectiveTick(TimingPolicy::NextBar, tl) == 36, "3/8 next bar from tick 5 is 36");
}

// isDue: an op applies at the first tick at or after its effective tick.
void test_is_due() {
    check(!tp::isDue(24, 23), "not due before the boundary");
    check(tp::isDue(24, 24), "due exactly at the boundary");
    check(tp::isDue(24, 30), "due after the boundary (zero boundary error, Req 6.8)");
}

}  // namespace

int main() {
    test_immediate();
    test_next_beat();
    test_next_bar();
    test_meter_3_8();
    test_is_due();

    if (failures == 0) {
        std::printf("example_timing_policy: all checks passed\n");
        return 0;
    }
    std::printf("example_timing_policy: %d check(s) failed\n", failures);
    return 1;
}
