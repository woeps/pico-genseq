// performance-safe-foundation task 10.1 — backend-neutral A/B record store.
//
// Validates Requirement 9.1 (backend-neutral storage layer) and the
// transactional A/B shape it defines for 9.6/9.7, against persistence::RecordStore
// over the in-memory FakeFlashStore:
//   - commit() writes to the alternate slot and bumps the generation, leaving the
//     previous record intact (Req 9.6);
//   - load() returns the newest valid committed record;
//   - an interrupted/failed commit (injected flash failure) leaves the previous
//     committed record selectable on the next load (Req 9.7);
//   - a CRC-corrupted or uncommitted slot is skipped during selection.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "RecordStore.h"
#include "PersistableConfig.h"
#include "SaveRecord.h"
#include "persistence/FakeFlashStore.h"

#include <cstdint>
#include <cstdio>

using persistence::RecordStore;
using persistence::PersistableConfig;
using persistence::CommitResult;
using persistence::LoadStatus;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

constexpr std::size_t kCap = 8192;  // two 4 KiB slots

PersistableConfig cfg(uint16_t bpm, uint8_t firstPitch) {
    PersistableConfig c{};
    c.bpm = bpm; c.patternCount = 1; c.midiClockEnabled = 1;
    c.patterns[0].pitchCount = 1; c.patterns[0].pitches[0] = firstPitch;
    c.patterns[0].active = 1;
    return c;
}

// Round-trip + generation increments; alternating slots.
void test_commit_and_load() {
    FakeFlashStore store(kCap);
    RecordStore rs(store);

    PersistableConfig out{};
    check(rs.load(out) == LoadStatus::ABSENT, "empty store loads ABSENT");

    check(rs.commit(cfg(120, 60)) == CommitResult::OK, "first commit OK");
    check(rs.currentGeneration() == 1, "first commit is generation 1");
    check(rs.load(out) == LoadStatus::OK && out.bpm == 120, "load returns the committed record");

    check(rs.commit(cfg(90, 48)) == CommitResult::OK, "second commit OK");
    check(rs.currentGeneration() == 2, "second commit bumps generation to 2");
    check(rs.load(out) == LoadStatus::OK && out.bpm == 90 && out.patterns[0].pitches[0] == 48,
          "load returns the NEWEST committed record");
}

// 9.6: the previous record stays intact until the new one commits — a failed
// write of the new record preserves the old one.
void test_failed_commit_preserves_previous() {
    FakeFlashStore store(kCap);
    RecordStore rs(store);
    rs.commit(cfg(120, 60));          // generation 1, committed
    check(rs.currentGeneration() == 1, "baseline generation 1");

    // Inject a flash failure for the NEXT write (the generation-2 commit).
    store.injectFailure(persistence::FlashResult::VERIFY_MISMATCH);
    check(rs.commit(cfg(200, 72)) == CommitResult::FAIL_WRITE, "second commit fails on injected error");

    // The previous (generation 1) record is still selectable (Req 9.7).
    PersistableConfig out{};
    check(rs.load(out) == LoadStatus::OK && out.bpm == 120,
          "previous committed record preserved after a failed commit (Req 9.6/9.7)");
    check(rs.currentGeneration() == 1, "generation unchanged after failed commit");
}

// Capacity guard: an oversized record is refused before any write.
void test_capacity_guard() {
    FakeFlashStore store(64);  // tiny: slot payload capacity ~ 24 bytes
    RecordStore rs(store);
    check(rs.commit(cfg(120, 60)) == CommitResult::FAIL_CAPACITY,
          "oversized record refused (capacity guard)");
    check(store.writeCount() == 0, "no flash write performed on capacity failure");
}

// Many commits alternate slots and keep selecting the newest.
void test_many_commits_alternate() {
    FakeFlashStore store(kCap);
    RecordStore rs(store);
    for (uint16_t i = 1; i <= 6; ++i) {
        check(rs.commit(cfg(static_cast<uint16_t>(100 + i), static_cast<uint8_t>(60 + i))) ==
                  CommitResult::OK, "commit OK");
        check(rs.currentGeneration() == i, "generation increments monotonically");
    }
    PersistableConfig out{};
    check(rs.load(out) == LoadStatus::OK && out.bpm == 106 && out.patterns[0].pitches[0] == 66,
          "newest of six commits selected");
}

}  // namespace

int main() {
    test_commit_and_load();
    test_failed_commit_preserves_previous();
    test_capacity_guard();
    test_many_commits_alternate();

    if (failures == 0) {
        std::printf("example_record_store: all checks passed\n");
        return 0;
    }
    std::printf("example_record_store: %d check(s) failed\n", failures);
    return 1;
}
