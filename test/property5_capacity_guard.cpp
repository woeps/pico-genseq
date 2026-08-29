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

#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// Generate one random VALID persistence::PatternConfig (same generator style as
// property2_store_roundtrip.cpp), but with pitch/velocity sets forced to their maximum length so
// each pattern contributes its worst-case ~44 payload bytes. This guarantees the serialized
// record dwarfs the tiny capacity used below, so RC_PRE almost never discards a case.
rc::Gen<persistence::PatternConfig> genFatPatternConfig() {
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

        // Pitch set: force the maximum length so the record is large.
        p.pitchCount = persistence::MAX_SET_LEN;
        p.pitchOrder = *rc::gen::inRange<int>(0, 4);  // common::PlayingOrder 0..3
        for (uint8_t i = 0; i < persistence::MAX_SET_LEN; i++) {
            p.pitches[i] = *rc::gen::inRange<int>(0, 128);  // MIDI-ish byte 0..127
        }

        // Velocity set: same maximum shape.
        p.velocityCount = persistence::MAX_SET_LEN;
        p.velocityOrder = *rc::gen::inRange<int>(0, 4);  // common::PlayingOrder 0..3
        for (uint8_t i = 0; i < persistence::MAX_SET_LEN; i++) {
            p.velocities[i] = *rc::gen::inRange<int>(0, 128);
        }

        p.midiChannel = *rc::gen::inRange<int>(1, 17);  // 1..16
        p.active = *rc::gen::inRange<int>(0, 2);        // 0/1
        return p;
    });
}

// Generate one random VALID persistence::PersistableConfig with patternCount in 1..MAX_PATTERNS
// (at least one pattern so the serialized record always exceeds the tiny capacity).
rc::Gen<persistence::PersistableConfig> genOversizedConfig() {
    return rc::gen::exec([] {
        persistence::PersistableConfig cfg{};
        const uint8_t patternCount =
            *rc::gen::inRange<int>(1, persistence::MAX_PATTERNS + 1);  // 1..15
        cfg.patternCount = patternCount;
        cfg.bpm = *rc::gen::inRange<int>(0, 1001);  // plausible BPM span 0..1000
        cfg.midiClockEnabled = *rc::gen::inRange<int>(0, 2);  // 0/1
        for (uint8_t i = 0; i < patternCount; i++) {
            cfg.patterns[i] = *genFatPatternConfig();
        }
        return cfg;
    });
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
