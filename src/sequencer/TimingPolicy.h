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
