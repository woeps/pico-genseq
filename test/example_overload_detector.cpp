// performance-safe-foundation task 4.3 — output backlog accounting + overload.
//
// Validates Requirements 4.4, 4.5, 4.6 against sequencer::OverloadDetector:
//   - 4.4: current and maximum backlog are tracked and exposed.
//   - 4.6: a backlog that stays non-empty for ONE BEAT of monotonic time latches
//     a sustained-overload signal (the caller then enters Timing Fault); a
//     backlog that drains before one beat does NOT latch (not sustained).
//   - 4.5 is structural (the bounded queue never grows) and is covered by
//     example_midi_event_queue; here we confirm the detector only ever reads a
//     bounded depth and never retains per-event state.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "OverloadDetector.h"

#include <cstdint>
#include <cstdio>

using sequencer::OverloadDetector;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

constexpr uint64_t kBeat = 500000;  // one beat = 0.5 s (120 BPM quarter note)

// 4.6: a backlog sustained for a full beat latches overload.
void test_sustained_overload_latches() {
    OverloadDetector d;
    check(!d.observe(5, 0, kBeat), "non-empty backlog at t=0 does not latch immediately");
    check(!d.observe(6, kBeat / 2, kBeat), "still under one beat -> no fault");
    check(!d.observe(6, kBeat - 1, kBeat), "one microsecond short of a beat -> no fault");
    check(d.observe(7, kBeat, kBeat), "backlog non-empty for a full beat -> overload latched (4.6)");
    check(d.overloaded(), "overload stays latched");
    // Latched even if the backlog later drops — only reset() clears it.
    check(d.observe(0, kBeat + 10, kBeat), "latched overload persists after latch");
}

// 4.6: a backlog that drains before one beat does NOT latch (transient burst).
void test_transient_backlog_no_fault() {
    OverloadDetector d;
    d.observe(8, 0, kBeat);
    d.observe(4, kBeat / 4, kBeat);
    check(!d.observe(0, kBeat / 2, kBeat), "backlog cleared before one beat -> no fault");
    // A new non-empty window starts fresh from here.
    d.observe(3, kBeat / 2 + 1, kBeat);
    check(!d.observe(3, kBeat, kBeat), "new window < one beat old -> still no fault");
    check(d.observe(3, kBeat / 2 + 1 + kBeat, kBeat),
          "the NEW window reaching one beat latches (window restarted on clear)");
}

// 4.4: current/max backlog accounting.
void test_backlog_metrics() {
    OverloadDetector d;
    d.observe(3, 0, kBeat);
    d.observe(9, 10, kBeat);
    d.observe(2, 20, kBeat);
    check(d.currentBacklog() == 2, "current backlog is the latest observation");
    check(d.maxBacklog() == 9, "max backlog is the peak observed");
    d.resetMetrics();
    check(d.maxBacklog() == 2, "resetMetrics drops max to the current depth");
}

// reset() clears the latch and the window.
void test_reset_clears_latch() {
    OverloadDetector d;
    d.observe(5, 0, kBeat);
    d.observe(5, kBeat, kBeat);
    check(d.overloaded(), "overload latched before reset");
    d.reset();
    check(!d.overloaded(), "reset clears the latch");
    // After reset, a fresh sustained window must latch again from scratch.
    d.observe(5, 2 * kBeat, kBeat);
    check(!d.observe(5, 3 * kBeat - 1, kBeat), "post-reset window < one beat -> no fault");
    check(d.observe(5, 3 * kBeat, kBeat), "post-reset window reaching one beat latches again");
}

}  // namespace

int main() {
    test_sustained_overload_latches();
    test_transient_backlog_no_fault();
    test_backlog_metrics();
    test_reset_clears_latch();

    if (failures == 0) {
        std::printf("example_overload_detector: all checks passed\n");
        return 0;
    }
    std::printf("example_overload_detector: %d check(s) failed\n", failures);
    return 1;
}
