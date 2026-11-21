// Feature: persist-settings, Property 5: capacity guard
//
// Validates: Requirements 1.6, 3.5, 4.2.
//
// Property 5 (design.md "Correctness Properties"): For any snapshot whose serialized Save_Record
// exceeds the Storage_Region capacity, save() reports a capacity failure, performs no erase or
// program, and leaves the previously stored bytes unchanged.
//
// How this file exercises it: PersistenceManager::save() serializes the snapshot first and, if
// bytes.size() > store.capacity(), returns FAIL_CAPACITY BEFORE ever calling
// eraseAndProgram() (see PersistenceManager.cpp). So we drive save() with a FakeFlashStore whose
// capacity is TINY (HEADER_SIZE + a few bytes) and a config whose serialized record is larger
// than that capacity. We snapshot the store's writeCount() and raw() bytes immediately before
// the oversized save and assert, afterwards, that the outcome is FAIL_CAPACITY, that no write
// occurred (writeCount unchanged), and that the region is byte-for-byte identical.
//
// A cfg with patternCount >= 1 and full pitch/velocity sets serializes to well over the tiny
// capacity (one pattern payload alone is ~44 bytes, plus the 12-byte header and 4-byte payload
// prefix), so it always exceeds a capacity chosen in [HEADER_SIZE, HEADER_SIZE + 8]. RC_PRE()
// guards the assertion so any case that happens to fit is discarded rather than mis-asserted,
// keeping every executed case meaningful.
//
// -------------------------------------------------------------------------------------------
// Shared test-runner arrangement (see test/test_main.cpp): each property file exposes a
// `bool runXxx()` entry point and NO main(); the single shared test/test_main.cpp aggregates
// them under one main(). This file therefore defines `runPersistProperty5()` and NO main().
// -------------------------------------------------------------------------------------------

#include "persistence/FakeFlashStore.h"
#include "persistence/PersistableConfig.h"
#include "persistence/PersistenceManager.h"
#include "persistence/SaveRecord.h"

#include "PersistGen.h"
#include "PersistTestSupport.h"

#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// Generate one random VALID v3 config (random library occupancy, set contents, and per-pattern
// references that may share sets). See PersistGen.h.
rc::Gen<persistence::PersistableConfig> genOversizedConfig() {
    return rc::gen::exec([] { return persist_gen::genConfig(1, true); });
}

}  // namespace

bool runPersistProperty5() {
    // Property 5: an oversized record never touches flash. We use a TINY store (capacity just
    // >= HEADER_SIZE so it is a plausible small region) and a config whose serialized size we
    // require, via RC_PRE, to exceed that capacity. Before the oversized save we snapshot the
    // store's write count and full byte image; afterward we assert FAIL_CAPACITY and that both
    // are unchanged. A guard that erased/programmed before checking capacity, or a wrong outcome
    // mapping, would trip one of these assertions.
    return rc::check(
        "Property 5: save() of a snapshot whose serialized record exceeds a tiny "
        "Storage_Region capacity returns FAIL_CAPACITY and touches no flash (writeCount and raw "
        "bytes unchanged)",
        [] {
            // Tiny capacity in [HEADER_SIZE, HEADER_SIZE + 8]: large enough to be a valid-ish
            // small region, far smaller than any multi-element pattern record.
            const std::size_t capacity =
                persistence::HEADER_SIZE +
                static_cast<std::size_t>(*rc::gen::inRange<int>(0, 9));  // +0..+8

            const persistence::PersistableConfig cfg = *genOversizedConfig();

            // Only meaningful when the record genuinely does not fit; discard the (rare) case
            // where it happens to fit within the tiny capacity.
            RC_PRE(persistence::serialize(cfg).size() > capacity);

            FakeFlashStore store(capacity);
            persistence::PersistenceManager manager(store);

            // Baseline captured immediately before the oversized attempt. The store starts
            // erased (all 0xFF); no prior write is required to prove "leaves previously stored
            // bytes unchanged" — the invariant is that this attempt changes nothing.
            const int writesBefore = store.writeCount();
            const std::vector<uint8_t> rawBefore = store.raw();

            const persistence::SaveOutcome outcome = manager.save(cfg);

            RC_ASSERT(outcome == persistence::SaveOutcome::FAIL_CAPACITY);
            RC_ASSERT(store.writeCount() == writesBefore);
            RC_ASSERT(store.raw() == rawBefore);
        });
}
