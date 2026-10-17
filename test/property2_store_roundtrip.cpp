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

#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>

namespace {

// Generous capacity matching FlashStore::STORAGE_SIZE (2 sectors * 4096 = 8192 bytes); every
// valid PersistableConfig serializes well within this, so save() never hits the capacity guard.
constexpr std::size_t kStorageSize = 8192;

// Generate one random VALID persistence::PatternConfig. Every field is drawn inside the range
// the schema documents (see PersistableConfig.h); only [0, count) set elements are filled
// meaningfully — matching the count-aware equality of PatternConfig::operator==.
rc::Gen<persistence::PatternConfig> genPatternConfig() {
    return rc::gen::exec([] {
        persistence::PatternConfig p{};

        // Gate generator parameters. gateSteps in 1..64 anchors the dependent ranges.
        p.gateAlgorithm = 0;  // only algorithm 0 today
        const uint8_t steps = *rc::gen::inRange<int>(1, 65);  // 1..64 inclusive
        p.gateSteps = steps;
        p.gatePulses = *rc::gen::inRange<int>(0, steps + 1);  // 0..steps
        p.gateRotation = *rc::gen::inRange<int>(0, steps);    // 0..steps-1
        p.gateNoteLength = *rc::gen::inRange<int>(0, 6);      // 0..5
        p.gateLength = *rc::gen::inRange<int>(0, 101);        // 0..100

        // Pitch set: count in 0..MAX_SET_LEN, only [0,count) contents matter.
        const uint8_t pitchCount =
            *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);
        p.pitchCount = pitchCount;
        p.pitchOrder = *rc::gen::inRange<int>(0, 4);  // common::PlayingOrder 0..3
        for (uint8_t i = 0; i < pitchCount; i++) {
            p.pitches[i] = *rc::gen::inRange<int>(0, 128);  // MIDI-ish byte 0..127
        }

        // Velocity set: same shape as pitch set.
        const uint8_t velocityCount =
            *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);
        p.velocityCount = velocityCount;
        p.velocityOrder = *rc::gen::inRange<int>(0, 4);  // common::PlayingOrder 0..3
        for (uint8_t i = 0; i < velocityCount; i++) {
            p.velocities[i] = *rc::gen::inRange<int>(0, 128);
        }

        p.midiChannel = *rc::gen::inRange<int>(1, 17);  // 1..16
        p.active = *rc::gen::inRange<int>(0, 2);        // 0/1
        return p;
    });
}

// Generate one random VALID persistence::PersistableConfig with patternCount in
// 0..MAX_PATTERNS; only [0, patternCount) patterns are filled meaningfully.
rc::Gen<persistence::PersistableConfig> genPersistableConfig() {
    return rc::gen::exec([] {
        persistence::PersistableConfig cfg{};
        const uint8_t patternCount =
            *rc::gen::inRange<int>(0, persistence::MAX_PATTERNS + 1);  // 0..15
        cfg.patternCount = patternCount;
        cfg.bpm = *rc::gen::inRange<int>(0, 1001);  // plausible BPM span 0..1000
        cfg.midiClockEnabled = *rc::gen::inRange<int>(0, 2);  // 0/1
        for (uint8_t i = 0; i < patternCount; i++) {
            cfg.patterns[i] = *genPatternConfig();
        }
        return cfg;
    });
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
