#pragma once

#include <cstdint>

#include "TransportTimeline.h"

// Timing Policy and effective-tick resolution (performance-safe-foundation
// task 7.1, Requirements 6.1, 6.8).
//
// A live operation applies at one of three musical boundaries resolved against
// the global Transport Timeline:
//
//   - Immediate : the next serviced tick (now).
//   - NextBeat  : the next tick index that is a beat boundary.
//   - NextBar   : the next tick index that is a bar boundary.
//
// `effectiveTick(policy, timeline)` returns the absolute logical tick index at
// which an operation requested *now* takes effect. The per-operation DEFAULT
// policies (Immediate for Play/Stop/panic/audition/Keep/Undo; Next Beat for
// mute/unmute; Next Bar for committed set replacement and removal) and the
// global override are task 7.2 / 7.3; this component owns only the boundary math
// so those tasks and the pending-operation queue build on one correct resolver.
//
// Boundary convention (zero logical-tick application error, Req 6.8): an
// operation requested when the timeline is EXACTLY on a boundary tick still
// applies at the NEXT boundary of that kind, never retroactively at the current
// tick, so queueing an op "on the boundary" is deterministic and never races the
// tick currently being serviced. Immediate applies at the current tick index.
//
// SDK-free: integer tick math over the timeline's meter; no Pico SDK types.

namespace sequencer {

enum class TimingPolicy : uint8_t {
    Immediate = 0,
    NextBeat = 1,
    NextBar = 2,
};

// The musical operations whose application boundary this subsystem resolves.
// Safety operations (Stop/panic/fault) are ALWAYS Immediate and ignore the
// global override; the musical ones carry an approved default (task 7.2) that
// the global override may replace (task 7.3).
enum class OperationKind : uint8_t {
    // Immediate by default and always immediate (safety / transport / live edit).
    Play,
    Stop,
    Panic,
    Audition,
    Keep,
    Undo,
    // Next Beat by default.
    Mute,
    Unmute,
    // Next Bar by default.
    SetReplacement,  // committed gate/pitch/velocity replacement
    Remove,
    SceneTransition,  // future scenes; default Next Bar (Req 6.6)
};

namespace timing_policy {

// Is this a SAFETY/immediate-locked operation? Play/Stop/panic/audition/Keep/
// Undo are Immediate by default (Req 6.2) AND are never shifted by the global
// override (Req 6.7, 7.3). The transport/live-edit set is treated as locked
// Immediate here so the override only moves the quantizable musical ops.
inline bool isImmediateLocked(OperationKind kind) {
    switch (kind) {
        case OperationKind::Play:
        case OperationKind::Stop:
        case OperationKind::Panic:
        case OperationKind::Audition:
        case OperationKind::Keep:
        case OperationKind::Undo:
            return true;
        default:
            return false;
    }
}

// Approved per-operation DEFAULT policy (task 7.2, Req 6.2-6.6).
inline TimingPolicy defaultPolicy(OperationKind kind) {
    switch (kind) {
        case OperationKind::Play:
        case OperationKind::Stop:
        case OperationKind::Panic:
        case OperationKind::Audition:
        case OperationKind::Keep:
        case OperationKind::Undo:
            return TimingPolicy::Immediate;        // Req 6.2
        case OperationKind::Mute:
        case OperationKind::Unmute:
            return TimingPolicy::NextBeat;          // Req 6.3
        case OperationKind::SetReplacement:
        case OperationKind::Remove:
        case OperationKind::SceneTransition:
            return TimingPolicy::NextBar;           // Req 6.4-6.6
    }
    return TimingPolicy::Immediate;
}

// Resolve the EFFECTIVE policy for an operation under the global override
// (task 7.3, Req 6.7). A safety/immediate-locked op always resolves to
// Immediate and ignores the override. A musical op uses the override when one is
// active, else its approved default. `overrideActive=false` means "use defaults".
inline TimingPolicy effectivePolicy(OperationKind kind, bool overrideActive,
                                    TimingPolicy overridePolicy) {
    if (isImmediateLocked(kind)) return TimingPolicy::Immediate;  // safety stays immediate
    return overrideActive ? overridePolicy : defaultPolicy(kind);
}

}  // namespace timing_policy

namespace timing_policy {

// The absolute logical tick at which an op requested at `currentTick` applies
// under `policy`, given `periodTicks` (ticksPerBeat or ticksPerBar). The next
// boundary STRICTLY after the current tick when on a boundary, else the next
// multiple of periodTicks at or after the current tick.
inline uint64_t nextBoundaryTick(uint64_t currentTick, uint32_t periodTicks) {
    if (periodTicks == 0) return currentTick;
    // Floor the current tick to its boundary, then advance one full period so an
    // op requested exactly on a boundary lands on the NEXT one (deterministic,
    // never the tick being serviced now).
    const uint64_t flooredBoundary = (currentTick / periodTicks) * periodTicks;
    return flooredBoundary + periodTicks;
}

// Resolve the effective tick for `policy` against `timeline`'s current position
// and meter.
inline uint64_t effectiveTick(TimingPolicy policy, const TransportTimeline& timeline) {
    const uint64_t current = timeline.tickIndex();
    switch (policy) {
        case TimingPolicy::Immediate:
            return current;  // applies at the current tick
        case TimingPolicy::NextBeat:
            return nextBoundaryTick(current, timeline.ticksPerBeat());
        case TimingPolicy::NextBar:
            return nextBoundaryTick(current, timeline.ticksPerBar());
    }
    return current;
}

// Has an operation whose effective tick is `effective` reached its boundary at
// the scheduler's `currentTick`? True once currentTick >= effective, so the
// scheduler applies it on the first tick at or after the boundary (Req 6.8).
inline bool isDue(uint64_t effective, uint64_t currentTick) {
    return currentTick >= effective;
}

}  // namespace timing_policy

}  // namespace sequencer
