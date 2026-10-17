// performance-safe-foundation task 7.5 — boundary + coalescing integration.
//
// Validates Requirements 6.1-6.9, 11.2 by combining TransportTimeline +
// TimingPolicy + CommandInbox: operations queued before, on, and after beat/bar
// boundaries across supported meters resolve to the correct effective tick with
// ZERO logical-tick application error, and compatible edits coalesce latest-wins
// without crossing barriers.
//
// Property A (boundary resolution, Req 6.1/6.8): for any supported meter, any
// current tick, and any policy, the resolved effectiveTick is a correct multiple
// of the period, strictly after the current tick when on a boundary, and isDue()
// becomes true exactly at that tick and never before.
//
// Property B (coalescing by boundary key, Req 6.9/7.6/7.7): a run of edits to one
// (PatternId, property) with the SAME effective boundary coalesces to a single
// pending op carrying the latest payload; a barrier between two edits keeps both.

#include "TimingPolicy.h"
#include "TransportTimeline.h"
#include "CommandInbox.h"
#include "../common/TimeSignature.h"
#include "../common/PatternId.h"

#include <rapidcheck.h>

#include <array>
#include <cstdint>
#include <vector>

namespace tp = sequencer::timing_policy;
using sequencer::TimingPolicy;
using sequencer::TransportTimeline;

namespace {

constexpr std::array<uint8_t, 6> kDenoms{1, 2, 4, 8, 16, 32};

TransportTimeline makeTimeline(uint8_t num, uint8_t denom, uint64_t tickIndex) {
    TransportTimeline tl;
    tl.setBpm(120);
    tl.setTimeSignature(common::TimeSignature(num, denom));
    tl.start(0);
    for (uint64_t i = 0; i < tickIndex; ++i) tl.advance();
    return tl;
}

}  // namespace

bool runProperty7Boundary() {
    bool ok = true;

    // (A) effectiveTick + isDue correctness across meters, positions, policies.
    ok &= rc::check(
        "Property 7 (boundary): effectiveTick resolves to the correct period "
        "multiple with zero logical-tick error, and isDue fires exactly there",
        [](uint8_t numRaw, uint8_t denomSel, uint16_t tickRaw, uint8_t polSel) {
            const uint8_t num = static_cast<uint8_t>(1 + (numRaw % 32));
            const uint8_t denom = kDenoms[denomSel % kDenoms.size()];
            const uint64_t current = tickRaw % 4096;
            const TimingPolicy policy = static_cast<TimingPolicy>(polSel % 3);

            TransportTimeline tl = makeTimeline(num, denom, current);
            const uint64_t eff = tp::effectiveTick(policy, tl);

            if (policy == TimingPolicy::Immediate) {
                RC_ASSERT(eff == current);  // applies now
            } else {
                const uint32_t period = (policy == TimingPolicy::NextBeat)
                                            ? tl.ticksPerBeat()
                                            : tl.ticksPerBar();
                RC_ASSERT(period > 0);
                // Effective tick is a clean multiple of the period...
                RC_ASSERT(eff % period == 0);
                // ...strictly after the current tick (deterministic on-boundary
                // rule: never the tick being serviced now)...
                RC_ASSERT(eff > current);
                // ...and no more than one full period away (the NEXT boundary).
                RC_ASSERT(eff - current <= period);
            }

            // isDue is false before the boundary and true exactly at/after it:
            // zero logical-tick application error (Req 6.8).
            if (eff > 0) RC_ASSERT(!tp::isDue(eff, eff - 1));
            RC_ASSERT(tp::isDue(eff, eff));
            RC_ASSERT(tp::isDue(eff, eff + 5));
        });

    // (B) Coalescing by (target, property): same-boundary edits collapse latest-
    // wins; a barrier keeps both sides.
    ok &= rc::check(
        "Property 7 (coalesce): same-key edits coalesce to the latest payload; a "
        "barrier between two same-key edits keeps both",
        [](const std::vector<uint8_t>& payloads, bool withBarrier) {
            RC_PRE(payloads.size() >= 2);
            sequencer::CommandInboxT<64, 16> inbox;
            const common::PatternId id(1, 1);
            constexpr uint8_t kProp = 2;

            // Push the first edit.
            sequencer::InboxCommand e; e.target = id; e.kind = 1; e.property = kProp;
            e.a = payloads.front();
            inbox.tryPushCoalescing(e);

            if (withBarrier) {
                sequencer::InboxCommand bar; bar.target = id; bar.kind = 9; bar.isBarrier = true;
                inbox.tryPushCoalescing(bar);
            }

            // Push the rest to the SAME key.
            for (size_t i = 1; i < payloads.size(); ++i) {
                sequencer::InboxCommand c; c.target = id; c.kind = 1; c.property = kProp;
                c.a = payloads[i];
                inbox.tryPushCoalescing(c);
            }

            if (withBarrier) {
                // First edit on one side, then the post-barrier run coalesced to
                // one: at least two slots survive (never coalesced across barrier).
                RC_ASSERT(inbox.count() >= 2);
            } else {
                // All edits to the same key, no barrier -> one coalesced slot.
                RC_ASSERT(inbox.count() == 1);
                sequencer::InboxCommand out;
                RC_ASSERT(inbox.tryPop(out));
                RC_ASSERT(out.a == payloads.back());  // latest-wins
            }
        });

    return ok;
}
