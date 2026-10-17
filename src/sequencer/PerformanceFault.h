#pragma once

#include <cstdint>

#include "PerformanceMetrics.h"

// Latched fail-closed Timing Fault (performance-safe-foundation task 5.3,
// Requirements 3.7, 10.1-10.7).
//
// When scheduling lateness reaches one beat (Req 3.7) or the MIDI output stays
// overloaded for one beat (Req 4.6), the system must FAIL CLOSED: stop producing
// new Pattern events, release all note ownership, emit a MIDI Stop when MIDI
// Clock Output is enabled, PRESERVE Working State, latch a machine-readable
// cause plus the relevant measurement, and NOT restart automatically. Only an
// explicit Play acknowledges the fault, resets runtime positions, and starts a
// fresh timeline (Req 10.6).
//
// `PerformanceFault` owns ONLY the latch, the cause, and the latched measurement
// — the pure state machine. The side effects (releaseAll, optional Stop, timeline
// reset) are performed by the caller (the Sequencer) when enter()/acknowledge()
// report a transition, so this component stays SDK-free and host-testable with no
// dependency on NoteLifecycle, the MIDI output, or the timeline.
//
// Idempotence (Req 10 "entering is idempotent"): enter() while already latched
// updates nothing and reports no new transition, so a repeated fault trigger does
// not emit a second Stop or re-run cleanup.

namespace sequencer {

// What the caller must do in response to a fault-state call.
struct FaultTransition {
    bool entered = false;        // just transitioned stopped/running -> faulted
    bool acknowledged = false;   // just transitioned faulted -> cleared (via Play)
};

class PerformanceFault {
public:
    // Latch a fault with its cause and the measurement that triggered it (the
    // lateness in microseconds, or the sustained-overload backlog). Idempotent:
    // a second enter() while latched changes nothing and reports entered=false,
    // so the caller runs the one-time cleanup (releaseAll / optional Stop) only
    // on the FIRST entry.
    FaultTransition enter(PerformanceFaultCause cause, uint64_t measurement,
                          PerformanceMetrics& metrics) {
        FaultTransition t;
        if (latched_) return t;  // already faulted — idempotent, no new transition
        latched_ = true;
        cause_ = cause;
        measurement_ = measurement;
        metrics.recordFault(cause);  // latch cause into the metrics snapshot
        t.entered = true;
        return t;
    }

    // Explicit Play acknowledgement (Req 10.6). Clears the latch and reports the
    // transition so the caller resets runtime positions and starts a new
    // timeline. A no-op (acknowledged=false) when not currently faulted.
    FaultTransition acknowledge() {
        FaultTransition t;
        if (!latched_) return t;
        latched_ = false;
        cause_ = PerformanceFaultCause::NONE;
        measurement_ = 0;
        t.acknowledged = true;
        return t;
    }

    bool latched() const { return latched_; }
    PerformanceFaultCause cause() const { return cause_; }
    uint64_t measurement() const { return measurement_; }

private:
    bool latched_ = false;
    PerformanceFaultCause cause_ = PerformanceFaultCause::NONE;
    uint64_t measurement_ = 0;
};

}  // namespace sequencer
