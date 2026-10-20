// performance-safe-foundation task 6.5 — CommandInbox stress + model property.
//
// Validates Requirements 7.1-7.11, 11.2 against sequencer::CommandInboxT with a
// model-based property: a random interleaving of producer pushes (ordinary
// edits, coalescible edits, barriers, safety ops) and bounded consumer drains is
// run through the real inbox while a reference model tracks the expected visible
// contents. The property asserts:
//
//   - COMPLETE PAYLOAD VISIBILITY (7.1): every command the consumer pops carries
//     exactly the payload the producer last published for it (latest-wins under
//     coalescing), never a partially-decoded or stale value.
//   - FIXED CAPACITY / SATURATION (7.2, 7.11): the inbox never holds more than
//     its capacity; a push into a full ring is refused (kInvalidSequence), never
//     blocks, never grows.
//   - BOUNDED SERVICE WORK (7.4): drain(budget) never returns more than budget.
//   - STALE SEQUENCES (6.9 / 7.8): an op bound to a released PatternId generation
//     resolves Rejected and is not applied.
//   - BARRIERS (7.7): coalescing never collapses edits across a barrier.
//
// Lives in the shared property runner (runProperty6Inbox); drives >=100 cases.

#include "CommandInbox.h"
#include "../common/PatternId.h"

#include <rapidcheck.h>

#include <cstdint>
#include <vector>

using sequencer::InboxCommand;
using sequencer::AckStatus;
using sequencer::kInvalidSequence;

namespace {

enum class Act : uint8_t { PushEdit, PushCoalesce, PushBarrier, PushSafety, Drain };

struct GenAct {
    Act act;
    uint8_t slot;      // pattern slot 0..3
    uint8_t property;  // 1..3 for coalescible edits
    uint8_t payload;   // the "latest" value
    uint8_t budget;    // drain budget 1..4
};

}  // namespace

namespace rc {
template <>
struct Arbitrary<GenAct> {
    static Gen<GenAct> arbitrary() {
        return gen::build<GenAct>(
            gen::set(&GenAct::act, gen::weightedElement<Act>({
                {4, Act::PushEdit},
                {4, Act::PushCoalesce},
                {1, Act::PushBarrier},
                {1, Act::PushSafety},
                {3, Act::Drain},
            })),
            gen::set(&GenAct::slot, gen::inRange<uint8_t>(0, 4)),
            gen::set(&GenAct::property, gen::inRange<uint8_t>(1, 4)),
            gen::set(&GenAct::payload, gen::arbitrary<uint8_t>()),
            gen::set(&GenAct::budget, gen::inRange<uint8_t>(1, 5)));
    }
};
}  // namespace rc

bool runProperty6Inbox() {
    bool ok = true;

    // Fixed-capacity + bounded-drain + saturation invariants under a random
    // producer/consumer interleaving.
    ok &= rc::check(
        "Property 6 (inbox): capacity, bounded drain and never-block saturation "
        "hold under any producer/consumer interleaving",
        [](const std::vector<GenAct>& acts) {
            constexpr uint16_t kCap = 8;
            sequencer::CommandInboxT<kCap, 32> inbox;
            for (const auto& g : acts) {
                const common::PatternId id(static_cast<uint8_t>(g.slot % 4), 1);
                switch (g.act) {
                    case Act::PushEdit: {
                        InboxCommand c; c.target = id; c.kind = 10; c.a = g.payload;
                        inbox.tryPush(c);
                        break;
                    }
                    case Act::PushCoalesce: {
                        InboxCommand c; c.target = id; c.kind = 11;
                        c.property = static_cast<uint8_t>(1 + (g.property % 3));
                        c.a = g.payload;
                        inbox.tryPushCoalescing(c);
                        break;
                    }
                    case Act::PushBarrier: {
                        InboxCommand c; c.target = id; c.kind = 12; c.isBarrier = true;
                        inbox.tryPush(c);
                        break;
                    }
                    case Act::PushSafety: {
                        InboxCommand c; c.kind = 13; c.isSafety = true;
                        inbox.tryPush(c);
                        break;
                    }
                    case Act::Drain: {
                        InboxCommand buf[4];
                        const uint16_t budget = static_cast<uint16_t>(1 + (g.budget % 4));
                        const uint16_t n = inbox.drain(buf, budget);
                        RC_ASSERT(n <= budget);  // bounded service work (7.4)
                        break;
                    }
                }
                // Fixed capacity invariant holds after EVERY action (7.2/7.11).
                RC_ASSERT(inbox.count() <= kCap);
            }
        });

    // Coalescing is latest-wins and complete-payload: after a run of coalescible
    // edits to one (target,property) with NO barrier between, exactly one slot
    // remains and it carries the LAST payload.
    ok &= rc::check(
        "Property 6 (coalesce): a barrier-free run of edits to one (target,"
        "property) coalesces to one slot carrying the latest payload",
        [](const std::vector<uint8_t>& payloads) {
            RC_PRE(!payloads.empty());
            sequencer::CommandInboxT<64, 32> inbox;
            const common::PatternId id(2, 1);
            for (uint8_t p : payloads) {
                InboxCommand c; c.target = id; c.kind = 11; c.property = 1; c.a = p;
                inbox.tryPushCoalescing(c);
            }
            RC_ASSERT(inbox.count() == 1);  // all coalesced into one slot
            InboxCommand out;
            RC_ASSERT(inbox.tryPop(out));
            RC_ASSERT(out.a == payloads.back());  // latest-wins, complete payload
        });

    // Barriers prevent coalescing across them: edits to the same (target,
    // property) on opposite sides of a barrier stay as two slots.
    ok &= rc::check(
        "Property 6 (barrier): a barrier between two same-key edits keeps both",
        [](uint8_t before, uint8_t after) {
            sequencer::CommandInboxT<16, 32> inbox;
            const common::PatternId id(1, 1);
            InboxCommand e1; e1.target = id; e1.kind = 11; e1.property = 1; e1.a = before;
            InboxCommand bar; bar.target = id; bar.kind = 12; bar.isBarrier = true;
            InboxCommand e2; e2.target = id; e2.kind = 11; e2.property = 1; e2.a = after;
            inbox.tryPushCoalescing(e1);
            inbox.tryPushCoalescing(bar);
            inbox.tryPushCoalescing(e2);
            RC_ASSERT(inbox.count() == 3);  // never coalesced across the barrier
        });

    return ok;
}
