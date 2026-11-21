// Feature: persist-settings, Property 1: serialization round-trip
//
// Validates: Requirements 2.3, 3.2, 3.3, 3.4, 5.3.
//
// Property 1 (design.md "Correctness Properties"): For any valid PersistableConfig snapshot,
// serializing it into a Save_Record and then deserializing that record produces a
// PersistableConfig in which the pattern count, sequencer BPM, MIDI clock enable flag, and —
// for every pattern — the gate parameters, pitch count/order/contents, velocity
// count/order/contents, MIDI channel, and active flag are each identical to the original
// snapshot. Concretely: deserialize(serialize(cfg), out) == LoadStatus::OK and out == cfg
// (PersistableConfig::operator== already performs the count-aware, field-by-field comparison).
//
// -------------------------------------------------------------------------------------------
// Shared test-runner arrangement (see test/test_main.cpp):
//
// RapidCheck's bare `rc::check` does NOT bundle a test-runner main(), and all property tests
// in this repo are swept into a SINGLE `genseq_property_tests` executable by test/CMakeLists.txt's
// `file(GLOB test/*.cpp)`. Two files each defining their own main() would fail to link
// (duplicate symbol). So each property file exposes a `bool runXxx()` entry point (true = all of
// that file's properties passed) and the single shared test/test_main.cpp aggregates them under
// one main(). This file therefore defines `runPersistProperty1()` and NO main(). The name is
// distinct from the sibling midi-output-interface feature's `runProperty1()`.
// -------------------------------------------------------------------------------------------

#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"

#include "PersistGen.h"
#include "PersistTestSupport.h"

#include <rapidcheck.h>

#include <cstdint>

namespace {

// Generate one random VALID v3 config (random library occupancy, set contents, and per-pattern
// references that may share sets). See PersistGen.h.
rc::Gen<persistence::PersistableConfig> genPersistableConfig() {
    return rc::gen::exec([] { return persist_gen::genConfig(0, false); });
}

}  // namespace

bool runPersistProperty1() {
    // Property 1: serializing then deserializing any valid snapshot yields an OK status and a
    // config equal to the original in every meaningful field. RapidCheck runs >=100 cases by
    // default. A dropped/altered/misplaced field would make either the status non-OK or the
    // count-aware operator== fail.
    return rc::check(
        "Property 1: deserialize(serialize(cfg)) yields OK and a config equal field-by-field "
        "to the original valid PersistableConfig snapshot",
        [] {
            const persistence::PersistableConfig cfg = *genPersistableConfig();

            const std::vector<uint8_t> bytes = persistence::serialize(cfg);

            persistence::PersistableConfig out{};
            const persistence::LoadStatus status =
                persistence::deserialize(bytes.data(), bytes.size(), out);

            RC_ASSERT(status == persistence::LoadStatus::OK);
            RC_ASSERT(out == cfg);
        });
}
