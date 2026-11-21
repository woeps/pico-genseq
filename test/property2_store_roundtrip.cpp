// Feature: persist-settings, Property 2: store round-trip
//
// Validates: Requirements 4.6, 5.4, 5.5.
//
// Property 2 (design.md "Correctness Properties"): For any valid PersistableConfig, performing a
// successful save(cfg) into an IFlashStore and then a load() from the same store yields a config
// equal in every field to cfg. This exercises the full serialize -> program -> read-back ->
// deserialize path through the store boundary (PersistenceManager over FakeFlashStore).
// Concretely: manager.save(cfg) == SaveOutcome::SUCCESS, then manager.load(out) == LoadStatus::OK
// and out == cfg (PersistableConfig::operator== performs the count-aware, field-by-field compare).
//
// -------------------------------------------------------------------------------------------
// Shared test-runner arrangement (see test/test_main.cpp): each property file exposes a
// `bool runXxx()` entry point and NO main(); the single shared test/test_main.cpp aggregates
// them under one main(). This file therefore defines `runPersistProperty2()` and NO main().
// -------------------------------------------------------------------------------------------

#include "persistence/FakeFlashStore.h"
#include "persistence/PersistableConfig.h"
#include "persistence/PersistenceManager.h"

#include "PersistGen.h"
#include "PersistTestSupport.h"

#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>

namespace {

// Generous capacity matching FlashStore::STORAGE_SIZE (2 sectors * 4096 = 8192 bytes); every
// valid PersistableConfig serializes well within this, so save() never hits the capacity guard.
constexpr std::size_t kStorageSize = 8192;

// Generate one random VALID v3 config (random library occupancy, set contents, and per-pattern
// references that may share sets). See PersistGen.h.
rc::Gen<persistence::PersistableConfig> genPersistableConfig() {
    return rc::gen::exec([] { return persist_gen::genConfig(0, false); });
}

}  // namespace

bool runPersistProperty2() {
    // Property 2: saving any valid snapshot into the store and then loading it back yields the
    // same config field-by-field. RapidCheck runs >=100 cases by default. A dropped/altered
    // field, a broken program/read-back path, or a decode error would make either save() report
    // a non-SUCCESS outcome, load() return a non-OK status, or the count-aware operator== fail.
    return rc::check(
        "Property 2: save(cfg) then load() through a FakeFlashStore yields SUCCESS/OK and a "
        "config equal field-by-field to the original valid PersistableConfig snapshot",
        [] {
            const persistence::PersistableConfig cfg = *genPersistableConfig();

            FakeFlashStore store(kStorageSize);
            persistence::PersistenceManager manager(store);

            const persistence::SaveOutcome outcome = manager.save(cfg);
            RC_ASSERT(outcome == persistence::SaveOutcome::SUCCESS);

            persistence::PersistableConfig out{};
            const persistence::LoadStatus status = manager.load(out);

            RC_ASSERT(status == persistence::LoadStatus::OK);
            RC_ASSERT(out == cfg);
        });
}
