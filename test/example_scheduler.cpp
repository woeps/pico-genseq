// performance-safe-foundation task 5.1 — Scheduler::service(now) front end.
//
// Validates Requirements 3.1, 3.2, 3.3 against sequencer::Scheduler:
//   - 3.1: every serviced tick carries an INTENDED monotonic timestamp owned by
//     the TransportTimeline (the exact rational deadline), independent of the
//     observation time passed to service().
//   - 3.3: immediate SAFETY WORK runs first on every service() call, before any
//     tick advances; and the per-service work is BOUNDED by a documented tick
//     budget — more overdue ticks than the budget are not drained in one call,
//     the surplus is reported, and the next call continues from the
//     authoritative phase (no unbounded catch-up burst).
//   - 3.2 (phase): servicing a late poll advances from the intended deadline, so
//     the next intended tick stays on the drift-free timeline phase.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "Scheduler.h"
#include "TransportTimeline.h"
#include "PerformanceMetrics.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using sequencer::Scheduler;
using sequencer::TransportTimeline;
using sequencer::PerformanceMetrics;
using sequencer::ServiceReport;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// A capturing tick sink that records each tick's intended timestamp.
struct TickSink {
    std::vector<uint64_t>* intended;
    void operator()(uint64_t intendedMicros) const { intended->push_back(intendedMicros); }
};
struct SafetyCounter {
    int* count;
    void operator()() const { ++(*count); }
};

template <typename S, typename T>
ServiceReport runService(TransportTimeline& tl, PerformanceMetrics& m, uint64_t now,
                         uint32_t budget, S safety, T tick) {
    Scheduler<S, T> sched(tl, m, budget);
    return sched.service(now, static_cast<S&&>(safety), static_cast<T&&>(tick));
}

// 3.3: safety work runs first, every call, even when no tick is due or the
// timeline is stopped.
void test_safety_runs_first_always() {
    TransportTimeline tl;
    PerformanceMetrics m;
    int safety = 0;
    std::vector<uint64_t> ticks;

    // Stopped timeline: no ticks, but safety still runs.
    runService(tl, m, 1000, 8, SafetyCounter{&safety}, TickSink{&ticks});
    check(safety == 1, "safety runs even when the timeline is stopped");
    check(ticks.empty(), "no ticks serviced while stopped");

    tl.setBpm(120);
    tl.start(0);
    // Now < first deadline: still no tick, safety runs again.
    runService(tl, m, 1, 8, SafetyCounter{&safety}, TickSink{&ticks});
    check(safety == 2, "safety runs even when no tick is yet due");
    check(ticks.empty(), "no tick due at t=1");
}

// 3.1 / 3.2: a serviced tick carries its intended (deadline) timestamp, not the
// observation time; servicing stays on the drift-free phase.
void test_intended_timestamps() {
    TransportTimeline tl;
    PerformanceMetrics m;
    tl.setBpm(120);
    tl.start(0);
    const uint64_t dur = tl.currentTickDurationMicros();  // ~20833

    int safety = 0;
    std::vector<uint64_t> ticks;
    // Observe LATE (deadline + 999) but only one tick is due.
    runService(tl, m, dur + 999, 8, SafetyCounter{&safety}, TickSink{&ticks});
    check(ticks.size() == 1, "exactly one tick due at ~1 period late");
    check(ticks[0] == dur, "tick carries its INTENDED deadline timestamp, not the late observation time");
    check(m.logicalTicksServiced == 1, "metrics counted one serviced tick");
    check(m.maxSchedulerLatenessUs == 999, "lateness measured against the intended deadline");
}

// 3.3: the per-service work budget bounds how many ticks one call advances; the
// surplus is reported and the next call continues from the authoritative phase.
void test_bounded_work_budget() {
    TransportTimeline tl;
    PerformanceMetrics m;
    tl.setBpm(120);
    tl.start(0);
    const uint64_t dur = tl.currentTickDurationMicros();
    const auto deadline = [](uint16_t bpm, uint64_t n) -> uint64_t {
        return (n * 60ULL * 1000000ULL) /
               (static_cast<uint64_t>(bpm) * sequencer::TransportTimeline::PPQN);
    };

    int safety = 0;
    std::vector<uint64_t> ticks;
    // 10 ticks are overdue, but the budget is 3 -> only 3 serviced this call.
    const uint64_t now = 10 * dur + 5;
    ServiceReport r = runService(tl, m, now, 3, SafetyCounter{&safety}, TickSink{&ticks});
    check(r.ticksServiced == 3, "budget caps ticks serviced per call at 3");
    check(ticks.size() == 3, "exactly 3 ticks dispatched this call");
    check(r.budgetExhausted, "budget-exhausted flag set when more ticks remain due");
    check(r.remainingOverdue > 0, "remaining overdue backlog reported, not drained");
    // The 3 serviced ticks carried the exact rational intended deadlines of
    // ticks 1, 2, 3. (The next-tick deadline is currentTickMicros + duration, so
    // the first serviced edge after start(0) is tick 1 at deadline(1), matching
    // the sequencer's convention that tick 0 is the Play instant itself.)
    check(ticks[0] == deadline(120, 1) && ticks[1] == deadline(120, 2) &&
          ticks[2] == deadline(120, 3),
          "serviced ticks carry sequential intended deadlines");

    // Next call continues from the authoritative phase (tick index 4 onward).
    std::vector<uint64_t> ticks2;
    runService(tl, m, now, 3, SafetyCounter{&safety}, TickSink{&ticks2});
    check(ticks2.size() == 3, "second call services the next 3 overdue ticks");
    check(ticks2[0] == deadline(120, 4), "second call resumes at the next intended deadline");
    check(m.logicalTicksServiced == 6, "metrics accumulate across calls");
}

// Default budget is the documented constant.
void test_default_budget() {
    TransportTimeline tl;
    PerformanceMetrics m;
    Scheduler<SafetyCounter, TickSink> sched(tl, m);
    check(sched.tickBudget() == Scheduler<SafetyCounter, TickSink>::kDefaultTickBudget,
          "default tick budget == kDefaultTickBudget");
    check(Scheduler<SafetyCounter, TickSink>::kDefaultTickBudget == 8,
          "documented default budget is 8 ticks per service");
}

}  // namespace

int main() {
    test_safety_runs_first_always();
    test_intended_timestamps();
    test_bounded_work_budget();
    test_default_budget();

    if (failures == 0) {
        std::printf("example_scheduler: all checks passed\n");
        return 0;
    }
    std::printf("example_scheduler: %d check(s) failed\n", failures);
    return 1;
}
