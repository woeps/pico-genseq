#pragma once

#include <cstdint>

#include "PerformanceMetrics.h"
#include "TransportTimeline.h"

// Deterministic scheduler front end (performance-safe-foundation task 5.1,
// Requirements 3.1, 3.2, 3.3).
//
// `Scheduler::service(nowMicros)` is the ONE component that advances musical
// time. It replaces the sequencer's inline "if (now >= nextTick) advance+tick"
// block with an explicit, host-testable unit that:
//
//   - gives every logical tick an INTENDED monotonic timestamp owned by the
//     TransportTimeline, independent of when firmware actually services it
//     (Req 3.1), and reports lateness against that intended time;
//
//   - prioritises IMMEDIATE SAFETY WORK before ordinary ticks (Req 3.3): a
//     safety callback (release notes / emit Stop / fault handling) runs first on
//     every service call, before any tick is advanced, so a release can never sit
//     behind queued musical work;
//
//   - performs a DOCUMENTED BOUNDED amount of work per service cycle (Req 3.3):
//     at most `maxTicksPerService` ticks are advanced in one call. If more ticks
//     are already overdue than the budget allows, the surplus is reported as
//     `remainingOverdue` so the caller/metrics can see the backlog; the next
//     service call continues from the authoritative phase. No unbounded catch-up
//     loop runs inside one call.
//
// This milestone's bounded under-one-beat reconciliation and the one-beat
// fail-closed fault (tasks 5.2 / 5.3) build ON this front end; here the tick
// callback is invoked once per serviced tick exactly as the current sequencer
// does, with lateness recorded. The component is a template over the two
// callbacks so it stays allocation-free and SDK-free (integer microseconds only,
// no std::function, no Pico SDK types).

namespace sequencer {

struct ServiceReport {
    uint32_t ticksServiced = 0;      // ticks advanced this call (<= budget)
    uint32_t remainingOverdue = 0;   // due ticks left unserviced by the budget
    uint64_t maxLatenessUs = 0;      // worst lateness observed this call
    bool budgetExhausted = false;    // true if the work budget capped the call
};

// SafetyWork: void(). Runs first, unconditionally, every service call.
// TickWork:   void(uint64_t intendedMicros). Runs once per serviced tick.
template <typename SafetyWork, typename TickWork>
class Scheduler {
public:
    Scheduler(TransportTimeline& timeline, PerformanceMetrics& metrics,
              uint32_t maxTicksPerService = kDefaultTickBudget)
        : timeline_(timeline), metrics_(metrics), budget_(maxTicksPerService) {}

    // Documented default per-service tick budget. One service cycle advances at
    // most this many ticks before yielding, bounding the work per call (Req 3.3).
    static constexpr uint32_t kDefaultTickBudget = 8;

    uint32_t tickBudget() const { return budget_; }

    // Advance the timeline up to the current monotonic time, bounded by the work
    // budget. `safety` runs first; `tick(intendedMicros)` runs once per serviced
    // tick. Returns a report of what happened for instrumentation and the later
    // reconciliation/fault stages.
    ServiceReport service(uint64_t nowMicros, SafetyWork&& safety, TickWork&& tick) {
        ServiceReport report;

        // 1) Immediate safety work ALWAYS runs first, before any tick advances
        //    (Req 3.3 — prioritise safety over ordinary edits/ticks).
        safety();

        if (!timeline_.running()) return report;

        // 2) Advance due ticks, bounded by the budget.
        while (report.ticksServiced < budget_) {
            const uint64_t dueTime = timeline_.nextTickMicros();
            if (nowMicros < dueTime) break;  // not yet due

            const uint64_t tickDurationUs = timeline_.currentTickDurationMicros();
            const uint64_t latenessUs = nowMicros - dueTime;
            const uint64_t missedTickCount =
                tickDurationUs ? (latenessUs / tickDurationUs) : 0;

            // The tick about to be serviced is intended at `dueTime`.
            timeline_.advance();
            const uint64_t intendedMicros = dueTime;

            metrics_.observeSchedulerTick(latenessUs, missedTickCount);
            if (latenessUs > report.maxLatenessUs) report.maxLatenessUs = latenessUs;

            tick(intendedMicros);
            ++report.ticksServiced;
        }

        // 3) If ticks remain due past the budget, report the backlog instead of
        //    draining it in an unbounded burst. The next service() continues.
        if (report.ticksServiced >= budget_ && nowMicros >= timeline_.nextTickMicros()) {
            report.budgetExhausted = true;
            uint32_t extra = 0;
            uint64_t probe = timeline_.nextTickMicros();
            const uint64_t dur = timeline_.currentTickDurationMicros();
            if (dur > 0) {
                // Count how many further ticks are already overdue (bounded by a
                // sane cap so a huge gap cannot spin — the fault path (5.3) owns
                // the >= one-beat case).
                while (nowMicros >= probe && extra < kOverdueProbeCap) {
                    probe += dur;
                    ++extra;
                }
            }
            report.remainingOverdue = extra;
        }

        return report;
    }

private:
    // Bound on how far the overdue-probe counts, so reporting the backlog is
    // itself bounded work. The one-beat fault (task 5.3) handles real runaway.
    static constexpr uint32_t kOverdueProbeCap = 1024;

    TransportTimeline& timeline_;
    PerformanceMetrics& metrics_;
    uint32_t budget_;
};

// Deduce a Scheduler from its two callables.
template <typename SafetyWork, typename TickWork>
Scheduler<SafetyWork, TickWork> makeScheduler(TransportTimeline& timeline,
                                              PerformanceMetrics& metrics,
                                              SafetyWork&&, TickWork&&) {
    return Scheduler<SafetyWork, TickWork>(timeline, metrics);
}

}  // namespace sequencer
