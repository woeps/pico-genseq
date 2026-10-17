// performance-safe-foundation tasks 5.1 / 5.2 / 5.3 — Scheduler::service(now).
//
// Validates Requirements 3.1-3.8 against sequencer::Scheduler:
//   - 5.1 (3.1-3.3): every serviced tick carries its INTENDED deadline timestamp
//     (not the observation time); SAFETY work runs first every call; per-service
//     work is BOUNDED by a documented tick budget, surplus reported not burst.
//   - 5.2 (3.4-3.6): a tick overdue by >= one period is RECONCILED (routed to the
//     reconcile callback) rather than run as a normal tick; only the final,
//     authoritative-phase tick (overdue by < one period) runs the normal tick
//     callback, so no catch-up burst of obsolete events is produced.
//   - 5.3 (3.7): lateness of ONE BEAT or more faults — service advances no tick
//     and reports faulted with the triggering lateness.
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

struct TickSink {
    std::vector<uint64_t>* intended;
    void operator()(uint64_t m) const { intended->push_back(m); }
};
struct SafetyCounter {
    int* count;
    void operator()() const { ++(*count); }
};

uint64_t deadline(uint16_t bpm, uint64_t n) {
    return (n * 60ULL * 1000000ULL) /
           (static_cast<uint64_t>(bpm) * sequencer::TransportTimeline::PPQN);
}

// Run service with recording tick + reconcile sinks and a safety counter.
template <typename S>
ServiceReport run(TransportTimeline& tl, PerformanceMetrics& m, uint64_t now, uint32_t budget,
                  S safety, std::vector<uint64_t>& ticks, std::vector<uint64_t>& recon) {
    Scheduler<S, TickSink, TickSink> sched(tl, m, budget);
    return sched.service(now, static_cast<S&&>(safety), TickSink{&ticks}, TickSink{&recon});
}

// 3.3: safety runs first on every call, even stopped or with no due tick.
void test_safety_runs_first_always() {
    TransportTimeline tl;
    PerformanceMetrics m;
    int safety = 0;
    std::vector<uint64_t> ticks, recon;

    run(tl, m, 1000, 8, SafetyCounter{&safety}, ticks, recon);
    check(safety == 1, "safety runs even when the timeline is stopped");
    check(ticks.empty() && recon.empty(), "no ticks serviced while stopped");

    tl.setBpm(120);
    tl.start(0);
    run(tl, m, 1, 8, SafetyCounter{&safety}, ticks, recon);
    check(safety == 2, "safety runs even when no tick is yet due");
    check(ticks.empty(), "no tick due at t=1");
}

// 3.1 / 3.2: a normal (on-phase) tick carries its intended deadline timestamp.
void test_intended_timestamps() {
    TransportTimeline tl;
    PerformanceMetrics m;
    tl.setBpm(120);
    tl.start(0);
    const uint64_t dur = tl.currentTickDurationMicros();  // ~20833

    int safety = 0;
    std::vector<uint64_t> ticks, recon;
    // Late by < one period: this is still the authoritative tick -> normal tick.
    run(tl, m, dur + 999, 8, SafetyCounter{&safety}, ticks, recon);
    check(ticks.size() == 1 && recon.empty(), "one normal tick (<1 period late), no reconcile");
    check(ticks[0] == dur, "tick carries its INTENDED deadline, not the late observation time");
    check(m.logicalTicksServiced == 1, "metrics counted one serviced tick");
    check(m.maxSchedulerLatenessUs == 999, "lateness measured against the intended deadline");
}

// 5.2: ticks overdue by >= one period are reconciled; only the final
// authoritative-phase tick runs the normal callback.
void test_reconciliation() {
    TransportTimeline tl;
    PerformanceMetrics m;
    tl.setBpm(120);
    tl.start(0);
    const uint64_t dur = tl.currentTickDurationMicros();

    int safety = 0;
    std::vector<uint64_t> ticks, recon;
    // Observe ~3.5 periods late: ticks 1,2,3 are each >= one period overdue
    // (reconciled), tick 4 is < one period late (authoritative -> normal tick).
    const uint64_t now = deadline(120, 4) + dur / 2;
    ServiceReport r = run(tl, m, now, 8, SafetyCounter{&safety}, ticks, recon);
    check(!r.faulted, "3.5 periods is well under one beat -> no fault");
    check(recon.size() == 3, "ticks 1,2,3 reconciled (>= one period overdue)");
    check(ticks.size() == 1, "exactly one authoritative-phase tick run normally");
    check(ticks[0] == deadline(120, 4), "the normal tick is tick 4 at its intended deadline");
    check(m.reconciliationCount == 3, "reconciliation count recorded in metrics");
    check(recon[0] == deadline(120, 1) && recon[1] == deadline(120, 2) &&
          recon[2] == deadline(120, 3), "reconciled ticks carry their intended deadlines");
}

// 5.1: bounded work budget caps ticks per call; the surplus is reported and the
// next call resumes from the authoritative phase. (All overdue here, so the
// budgeted ones are reconciled.)
void test_bounded_work_budget() {
    TransportTimeline tl;
    PerformanceMetrics m;
    tl.setBpm(120);
    tl.start(0);
    const uint64_t dur = tl.currentTickDurationMicros();

    int safety = 0;
    std::vector<uint64_t> ticks, recon;
    // 10 periods late, budget 3: only 3 ticks advanced this call (all reconciled,
    // being far overdue), surplus reported.
    const uint64_t now = deadline(120, 10) + 5;
    ServiceReport r = run(tl, m, now, 3, SafetyCounter{&safety}, ticks, recon);
    check(r.ticksServiced + r.ticksReconciled == 3, "budget caps advanced ticks at 3");
    check(recon.size() == 3 && ticks.empty(), "the 3 budgeted ticks are reconciled (far overdue)");
    check(r.budgetExhausted, "budget-exhausted flag set when more remain");
    check(r.remainingOverdue > 0, "surplus backlog reported, not drained");
    check(recon[0] == deadline(120, 1), "first advanced tick is tick 1's deadline");

    std::vector<uint64_t> ticks2, recon2;
    run(tl, m, now, 3, SafetyCounter{&safety}, ticks2, recon2);
    check(recon2.size() == 3, "second call advances the next 3 overdue ticks");
    check(recon2[0] == deadline(120, 4), "second call resumes at tick 4's deadline");
}

// 5.3: lateness of one beat or more faults; no tick is advanced in that call.
void test_one_beat_fault() {
    TransportTimeline tl;
    PerformanceMetrics m;
    tl.setBpm(120);               // 4/4 default -> ticksPerBeat == 24
    tl.start(0);
    const uint64_t dur = tl.currentTickDurationMicros();
    const uint32_t beatTicks = tl.ticksPerBeat();
    check(beatTicks == 24, "4/4 at PPQN 24 -> 24 ticks per beat");

    int safety = 0;
    std::vector<uint64_t> ticks, recon;
    // The first due tick (tick 1) is late by a full beat (>= 24 periods).
    const uint64_t now = deadline(120, 1) + static_cast<uint64_t>(beatTicks) * dur + 10;
    ServiceReport r = run(tl, m, now, 32, SafetyCounter{&safety}, ticks, recon);
    check(r.faulted, "lateness of one beat or more reports a fault (3.7)");
    check(ticks.empty() && recon.empty(), "no tick advanced in the faulting call");
    check(r.faultLatenessUs >= static_cast<uint64_t>(beatTicks) * dur,
          "reported fault lateness is at least one beat");
    check(safety == 1, "safety still ran before the fault check");
}

void test_default_budget() {
    TransportTimeline tl;
    PerformanceMetrics m;
    Scheduler<SafetyCounter, TickSink, TickSink> sched(tl, m);
    check(sched.tickBudget() == Scheduler<SafetyCounter, TickSink, TickSink>::kDefaultTickBudget,
          "default tick budget == kDefaultTickBudget");
    check(Scheduler<SafetyCounter, TickSink, TickSink>::kDefaultTickBudget == 8,
          "documented default budget is 8 ticks per service");
}

}  // namespace

int main() {
    test_safety_runs_first_always();
    test_intended_timestamps();
    test_reconciliation();
    test_bounded_work_budget();
    test_one_beat_fault();
    test_default_budget();

    if (failures == 0) {
        std::printf("example_scheduler: all checks passed\n");
        return 0;
    }
    std::printf("example_scheduler: %d check(s) failed\n", failures);
    return 1;
}
