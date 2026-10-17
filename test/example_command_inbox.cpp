// performance-safe-foundation tasks 6.1 / 6.2 — fixed-capacity CommandInbox.
//
// Validates Requirements 7.1-7.4, 7.8, 6.9 against sequencer::CommandInboxT:
//   - 6.1 (7.1-7.3): single-producer/single-consumer ring; a complete command is
//     published in one slot, so the consumer never blocks for payload words;
//     tryPush/tryPop never block and the ring is fixed-capacity (full is refused,
//     not grown).
//   - 6.1 (7.4): drain(budget) pops at most `budget` commands, bounding per-cycle
//     work.
//   - 6.2 (7.8): every pushed op gets a monotonic SequenceId; popping records it
//     Accepted; applying records Applied; core0 can poll the ack status.
//   - 6.2 (6.9 / 7.8): an op bound to a stale PatternId generation is Rejected and
//     NOT applied; a live-id op is Applied; safety ops bypass the id check.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "CommandInbox.h"
#include "../common/PatternId.h"

#include <cstdint>
#include <cstdio>

using sequencer::InboxCommand;
using sequencer::AckStatus;
using sequencer::kInvalidSequence;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

InboxCommand op(common::PatternId target, uint8_t kind, bool safety = false) {
    InboxCommand c;
    c.target = target;
    c.kind = kind;
    c.isSafety = safety;
    return c;
}

// 6.2: monotonic sequence IDs; FIFO pop; Accepted on pop.
void test_sequence_and_fifo() {
    sequencer::CommandInboxT<8, 16> inbox;
    common::PatternId id(0, 1);
    const auto s1 = inbox.tryPush(op(id, 1));
    const auto s2 = inbox.tryPush(op(id, 2));
    const auto s3 = inbox.tryPush(op(id, 3));
    check(s1 != kInvalidSequence && s2 == s1 + 1 && s3 == s2 + 1,
          "sequence IDs are monotonic and non-zero");
    check(inbox.count() == 3, "three commands staged");

    InboxCommand out;
    check(inbox.tryPop(out) && out.sequence == s1 && out.kind == 1, "FIFO: first pop is s1");
    check(inbox.ackStatus(s1) == AckStatus::Accepted, "popped sequence recorded Accepted");
    check(inbox.ackStatus(s2) == AckStatus::Unknown, "unpopped sequence has no ack yet");
}

// 6.1: fixed capacity — tryPush refuses when full and never grows.
void test_fixed_capacity() {
    sequencer::CommandInboxT<4, 16> inbox;  // holds 4
    common::PatternId id(1, 1);
    for (int i = 0; i < 4; ++i)
        check(inbox.tryPush(op(id, static_cast<uint8_t>(i))) != kInvalidSequence,
              "push accepted up to capacity");
    check(inbox.full(), "inbox reports full at capacity");
    check(inbox.tryPush(op(id, 99)) == kInvalidSequence, "push past capacity refused (not grown)");
    check(inbox.count() == 4, "count never exceeds capacity (fixed, bounded)");
}

// 6.1: bounded drain pops at most `budget`.
void test_bounded_drain() {
    sequencer::CommandInboxT<16, 32> inbox;
    common::PatternId id(2, 1);
    for (int i = 0; i < 10; ++i) inbox.tryPush(op(id, static_cast<uint8_t>(i)));
    InboxCommand buf[4];
    const uint16_t drained = inbox.drain(buf, 4);
    check(drained == 4, "drain returns at most the budget (4)");
    check(inbox.count() == 6, "drain left the remaining 6 commands");
    check(buf[0].kind == 0 && buf[3].kind == 3, "drained in FIFO order");
}

// 6.2 / 6.9: stale-generation rejection; live apply; safety bypass.
void test_stale_generation_rejection() {
    sequencer::CommandInboxT<8, 16> inbox;
    common::PatternIdAllocator alloc;
    const common::PatternId live = alloc.allocate();      // slot 0, gen 1
    // An op captured against `live` while it is current.
    const auto sLive = inbox.tryPush(op(live, 1));

    // Free and re-allocate the slot: the new occupant has a HIGHER generation,
    // so `live` is now stale.
    alloc.release(live);
    const common::PatternId reused = alloc.allocate();    // slot 0, gen 2
    check(reused.generation > live.generation, "reused slot has a higher generation");
    const auto sStale = inbox.tryPush(op(live, 2));        // stale target
    const auto sFresh = inbox.tryPush(op(reused, 3));      // live target
    const auto sSafety = inbox.tryPush(op(common::PatternId{}, 0, /*safety=*/true));

    InboxCommand c;
    inbox.tryPop(c);  // sLive
    check(inbox.resolveAndApply(c, alloc) == false,
          "op bound to a since-reused (stale) slot is NOT applied, even if popped");
    check(inbox.ackStatus(sLive) == AckStatus::Rejected, "stale op recorded Rejected (6.9)");

    inbox.tryPop(c);  // sStale
    check(!inbox.resolveAndApply(c, alloc), "second stale-generation op also rejected");
    check(inbox.ackStatus(sStale) == AckStatus::Rejected, "second stale op Rejected");

    inbox.tryPop(c);  // sFresh
    check(inbox.resolveAndApply(c, alloc), "op bound to the live id applies");
    check(inbox.ackStatus(sFresh) == AckStatus::Applied, "live op recorded Applied (7.8)");

    inbox.tryPop(c);  // sSafety
    check(inbox.resolveAndApply(c, alloc), "safety op bypasses the id check and applies");
    check(inbox.ackStatus(sSafety) == AckStatus::Applied, "safety op recorded Applied");
}

}  // namespace

int main() {
    test_sequence_and_fifo();
    test_fixed_capacity();
    test_bounded_drain();
    test_stale_generation_rejection();

    if (failures == 0) {
        std::printf("example_command_inbox: all checks passed\n");
        return 0;
    }
    std::printf("example_command_inbox: %d check(s) failed\n", failures);
    return 1;
}
