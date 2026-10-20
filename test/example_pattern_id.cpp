// performance-safe-foundation task 2.2 — stable PatternId runtime identity.
//
// Validates Requirements 5.1, 6.9, 7.8:
//   - a PatternId is stable: removing or reordering ANOTHER pattern does not retarget it, so
//     an operation or note-ownership record bound to a PatternId still names the same pattern;
//   - a slot freed and later reused comes back with a HIGHER generation, so a reference
//     captured against the previous occupant is detectably STALE and is rejected (not applied
//     to the slot's new occupant);
//   - the allocator is fixed-capacity and allocation-free, returning an invalid id when full.
//
// Standalone example (its own main(), no RapidCheck), builds host-side unconditionally and is
// excluded from the property-test glob.

#include "common/PatternId.h"

#include <cstdint>
#include <cstdio>

using common::PatternId;
using common::PatternIdAllocator;
using common::PATTERN_CAPACITY;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// A default/zero id is invalid; the first allocation is valid with a non-zero generation.
void test_validity() {
    check(!PatternId{}.valid(), "default PatternId is invalid");
    PatternIdAllocator alloc;
    const PatternId a = alloc.allocate();
    check(a.valid(), "first allocation is valid");
    check(a.generation != 0, "live generation is never 0");
    check(alloc.isLive(a), "freshly allocated id is live");
}

// Removing one pattern leaves OTHER patterns' identities untouched (Req 6.9/5.1): their id
// value is unchanged and still live, even though a vector index would have shifted.
void test_identity_stable_across_removal() {
    PatternIdAllocator alloc;
    const PatternId p0 = alloc.allocate();  // slot 0
    const PatternId p1 = alloc.allocate();  // slot 1
    const PatternId p2 = alloc.allocate();  // slot 2

    // Remove the MIDDLE pattern. In a vector, p2 would shift from index 2 to index 1.
    alloc.release(p1);

    check(!alloc.isLive(p1), "removed pattern is no longer live");
    check(alloc.isLive(p0), "pattern before the removed one is still live");
    check(alloc.isLive(p2), "pattern after the removed one is still live and UNMOVED");
    check(alloc.current(p2.index) == p2, "p2 still occupies its original slot with same id");
    check(alloc.current(p0.index) == p0, "p0 identity unchanged");
}

// A freed slot reused by a new pattern gets a strictly higher generation, so an operation
// still holding the OLD id is detected as stale and rejected (Req 7.8).
void test_stale_generation_rejected_on_reuse() {
    PatternIdAllocator alloc;
    const PatternId p0 = alloc.allocate();  // slot 0, gen g1
    alloc.allocate();                       // slot 1
    alloc.release(p0);                      // free slot 0

    const PatternId reused = alloc.allocate();  // reuses slot 0 with a new generation
    check(reused.index == p0.index, "freed slot is reused");
    check(reused.generation > p0.generation, "reused slot gets a strictly higher generation");

    // An operation bound to the OLD occupant must NOT be treated as targeting the new one.
    check(!alloc.isLive(p0), "stale id (old generation) is rejected");
    check(alloc.isLive(reused), "the new occupant's id is live");
    check(p0 != reused, "old and new ids are distinct despite sharing a slot");
}

// Fixed capacity: allocation never grows past PATTERN_CAPACITY and returns an invalid id when
// full; releasing frees capacity again.
void test_fixed_capacity() {
    PatternIdAllocator alloc;
    PatternId ids[PATTERN_CAPACITY];
    for (uint8_t i = 0; i < PATTERN_CAPACITY; ++i) {
        ids[i] = alloc.allocate();
        check(ids[i].valid(), "allocation within capacity is valid");
    }
    check(alloc.liveCount() == PATTERN_CAPACITY, "all slots occupied");

    const PatternId overflow = alloc.allocate();
    check(!overflow.valid(), "allocation past capacity returns an invalid id (no growth)");

    alloc.release(ids[3]);
    const PatternId refill = alloc.allocate();
    check(refill.valid(), "a released slot can be re-allocated");
    check(refill.index == ids[3].index, "re-allocation reuses the freed slot");
    check(refill.generation > ids[3].generation, "re-allocation bumps the generation");
}

}  // namespace

int main() {
    test_validity();
    test_identity_stable_across_removal();
    test_stale_generation_rejected_on_reuse();
    test_fixed_capacity();

    if (failures == 0) {
        std::printf("example_pattern_id: all checks passed\n");
        return 0;
    }
    std::printf("example_pattern_id: %d check(s) failed\n", failures);
    return 1;
}
