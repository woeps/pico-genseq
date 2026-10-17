// Feature: persist-settings, Property 6: load is read-only
//
// Validates: Requirements 6.5, 6.6.
//
// Property 6 (design.md "Correctness Properties"): For any Storage_Region contents, load()
// performs zero writes to the flash store, leaving the region byte-for-byte unchanged. Loading
// is strictly a read + validate + decode operation; whether the bytes happen to form a valid
// Save_Record (LoadStatus::OK) or garbage (any non-OK status), the region must never be mutated.
//
// How this file exercises it: PersistenceManager::load() reads HEADER_SIZE bytes, parses the
// declared payload length, reads the (clamped) record, and deserializes it — with no path that
// erases or programs (see PersistenceManager.cpp). We model "arbitrary flash content" by
// generating a random byte vector of a random length in [0, capacity] and programming it into a
// FakeFlashStore via eraseAndProgram(). Since FakeFlashStore has no setter for arbitrary
// contents (it starts all-0xFF and eraseAndProgram is the only writer), this both populates the
// region with valid-OR-garbage bytes and models a real prior write. We then snapshot the store's
// writeCount() and full raw() image AFTER that setup program and BEFORE the load, run load(), and
// assert nothing changed. A len of 0 leaves the region all-0xFF (an erased region), which is a
// meaningful edge case (maps to BAD_MAGIC -> defaults) still subject to the same read-only rule.
//
// We deliberately do NOT assert a particular LoadStatus: the property is strictly about
// read-only-ness, and random bytes may or may not happen to form a valid record.
//
// -------------------------------------------------------------------------------------------
// Shared test-runner arrangement (see test/test_main.cpp): each property file exposes a
// `bool runXxx()` entry point and NO main(); the single shared test/test_main.cpp aggregates
// them under one main(). This file therefore defines `runPersistProperty6()` and NO main().
// -------------------------------------------------------------------------------------------

#include "persistence/FakeFlashStore.h"
#include "persistence/PersistableConfig.h"
#include "persistence/PersistenceManager.h"

#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// Storage_Region capacity matching FlashStore::STORAGE_SIZE (2 sectors * 4096 = 8192 bytes).
constexpr std::size_t kStorageSize = 8192;

}  // namespace

bool runPersistProperty6() {
    // Property 6: load() never writes to the store. We seed the region with arbitrary content
    // (random bytes, random length in [0, capacity]) so the record may be valid or garbage,
    // snapshot the store immediately after seeding, run load(), and assert the write count and
    // every byte of the region are unchanged. Any accidental erase/program during load — or a
    // load path that "repaired" or rewrote the region — would trip one of these assertions.
    return rc::check(
        "Property 6: load() over any Storage_Region contents performs zero writes and leaves the "
        "region byte-for-byte unchanged",
        [] {
            // Random region contents of a random length in [0, capacity]. len == 0 leaves the
            // region fully erased (all 0xFF); len == capacity fills it completely.
            const std::size_t len = static_cast<std::size_t>(
                *rc::gen::inRange<int>(0, static_cast<int>(kStorageSize) + 1));
            const std::vector<uint8_t> content =
                *rc::gen::container<std::vector<uint8_t>>(
                    len, rc::gen::inRange<int>(0, 256));

            FakeFlashStore store(kStorageSize);
            // Program the arbitrary content (models valid-OR-garbage flash). This is the ONLY
            // write; the property is that load() adds no further writes beyond this baseline.
            store.eraseAndProgram(content.data(), content.size());

            persistence::PersistenceManager manager(store);

            // Baseline captured AFTER seeding and BEFORE the load.
            const int writesBefore = store.writeCount();
            const std::vector<uint8_t> rawBefore = store.raw();

            // Defaulted out; on any non-OK status the manager leaves it untouched, but here we
            // only care about the store side effects, not the decoded value.
            persistence::PersistableConfig out{};
            (void)manager.load(out);

            RC_ASSERT(store.writeCount() == writesBefore);
            RC_ASSERT(store.raw() == rawBefore);
        });
}
