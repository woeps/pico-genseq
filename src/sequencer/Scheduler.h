#pragma once

#include <cstdint>

#include "PerformanceMetrics.h"
#include "TransportTimeline.h"

// Deterministic scheduler front end (performance-safe-foundation tasks 5.1, 5.2,
// 5.3; Requirements 3.1-3.8).
//
// `Scheduler::service(nowMicros)` is the ONE component that advances musical
// time. It:
//
//   - gives every logical tick an INTENDED monotonic timestamp owned by the
//     TransportTimeline, independent of when firmware services it (Req 3.1), and
//     reports lateness against that intended time (Req 3.8);
//
//   - prioritises IMMEDIATE SAFETY WORK before ordinary ticks (Req 3.3): the
//     safety callback runs first on every service call, before any tick advances;
//
//   - performs a DOCUMENTED BOUNDED amount of work per service cycle (Req 3.3):
//     at most `maxTicksPerService` ticks are advanced in one call; surplus due
//     ticks are reported, not drained in an unbounded burst (task 5.1);
//
//   - RECONCILES bounded lateness BELOW ONE BEAT without obsolete output
//     (task 5.2, Req 3.4-3.6): when a tick is already overdue by MORE than its
//     own period (i.e. it and the ticks behind it were missed), the missed ticks
//     are advanced through the RECONCILE callback — which updates Pattern phase
//     and emits only the safety-critical releases whose boundary was crossed, NOT
//     obsolete Note On events — and only the final, authoritative-phase tick runs
//     the normal TICK callback. No catch-up burst of stale notes is produced;
//
//   - FAILS CLOSED AT ONE BEAT (task 5.3, Req 3.7): if the oldest due tick is
//     late by one beat or more, service stops advancing and reports a fault so
//     the caller latches PerformanceFault and runs fail-closed cleanup. No tick
//     is serviced in the faulting call.
//
// The component is a template over the callbacks so it stays allocation-free and
// SDK-free (integer microseconds only; no std::function, no Pico SDK types).

namespace sequencer {

struct ServiceReport {
    uint32_t ticksServiced = 0;       // normal (authoritative-phase) ticks run
    uint32_t ticksReconciled = 0;     // missed ticks advanced without obsolete Note On
    uint32_t remainingOverdue = 0;    // due ticks left unserviced by the budget
    uint64_t maxLatenessUs = 0;       // worst lateness observed this call
    bool budgetExhausted = false;     // the work budget capped the call
    bool faulted = false;             // lateness reached one beat -> caller must fail closed
    uint64_t faultLatenessUs = 0;     // lateness that triggered the fault
};

// SafetyWork: void().                 Runs first, unconditionally, every call.
// TickWork:   void(uint64_t intended). Normal on-phase tick.
// ReconcileWork: void(uint64_t intended). Missed tick: advance phase + crossed
//               releases only, NO obsolete Note On.
template <typename SafetyWork, typename TickWork, typename ReconcileWork>
class Scheduler {
public:
    Scheduler(TransportTimeline& timeline, PerformanceMetrics& metrics,
              uint32_t maxTicksPerService = kDefaultTickBudget)
        : timeline_(timeline), metrics_(metrics), budget_(maxTicksPerService) {}

    // Documented default per-service tick budget. One service cycle advances at
    // most this many ticks before yielding, bounding the work per call (Req 3.3).
    static constexpr uint32_t kDefaultTickBudget = 8;

    uint32_t tickBudget() const { return budget_; }

    ServiceReport service(uint64_t nowMicros, SafetyWork&& safety, TickWork&& tick,
                          ReconcileWork&& reconcile) {
        ServiceReport report;

        // 1) Immediate safety work ALWAYS runs first (Req 3.3).
        safety();

        if (!timeline_.running()) return report;

        // One beat of lateness is the fail-closed threshold (Req 3.7). ticksPerBeat
        // comes from the active meter; a tick late by >= beatTicks periods is a
        // one-beat-or-more fault.
        const uint32_t beatTicks = timeline_.ticksPerBeat();

        // 2) Advance due ticks, bounded by the budget.
        while (report.ticksServiced + report.ticksReconciled < budget_) {
            const uint64_t dueTime = timeline_.nextTickMicros();
            if (nowMicros < dueTime) break;  // not yet due

            const uint64_t tickDurationUs = timeline_.currentTickDurationMicros();
            const uint64_t latenessUs = nowMicros - dueTime;
            const uint64_t missedTickCount =
                tickDurationUs ? (latenessUs / tickDurationUs) : 0;

            // Fail-closed at one beat (Req 3.7): if THIS due tick is late by a
            // whole beat or more, do not service it — report a fault and stop.
            if (beatTicks > 0 && tickDurationUs > 0 &&
                latenessUs >= static_cast<uint64_t>(beatTicks) * tickDurationUs) {
                report.faulted = true;
                report.faultLatenessUs = latenessUs;
                if (latenessUs > report.maxLatenessUs) report.maxLatenessUs = latenessUs;
                return report;
            }

            const uint64_t intendedMicros = dueTime;
            timeline_.advance();

            metrics_.observeSchedulerTick(latenessUs, missedTickCount);
            if (latenessUs > report.maxLatenessUs) report.maxLatenessUs = latenessUs;

            // Reconciliation (Req 3.4-3.6): a tick that is itself overdue by more
            // than one period is a MISSED tick — the authoritative phase has moved
            // past it. Advance it through reconcile() (phase + crossed releases,
            // no obsolete Note On). The tick that is due but NOT yet a full period
            // late is the authoritative-phase tick and runs the normal callback.
            if (tickDurationUs > 0 && latenessUs >= tickDurationUs) {
                reconcile(intendedMicros);
                metrics_.recordReconciliation();
                ++report.ticksReconciled;
            } else {
                tick(intendedMicros);
                ++report.ticksServiced;
            }
        }

        // 3) Budget cap: if ticks remain due, report the backlog (task 5.1).
        if (report.ticksServiced + report.ticksReconciled >= budget_ &&
            nowMicros >= timeline_.nextTickMicros()) {
            report.budgetExhausted = true;
            uint32_t extra = 0;
            uint64_t probe = timeline_.nextTickMicros();
            const uint64_t dur = timeline_.currentTickDurationMicros();
            if (dur > 0) {
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
    static constexpr uint32_t kOverdueProbeCap = 1024;

    TransportTimeline& timeline_;
    PerformanceMetrics& metrics_;
    uint32_t budget_;
};

}  // namespace sequencer
