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

#include <rapidcheck.h>

#include <cstdint>

namespace {

// Generate one random VALID persistence::PatternConfig. Every field is drawn inside the range
// the schema documents (see PersistableConfig.h), and only [0, count) set elements are filled
// meaningfully — matching the count-aware equality of PatternConfig::operator==.
rc::Gen<persistence::PatternConfig> genPatternConfig() {
    return rc::gen::exec([] {
        persistence::PatternConfig p{};

        // Gate generator parameters. gateSteps in 1..64 anchors the dependent ranges.
        p.gateAlgorithm = 0;  // only algorithm 0 today
        const uint8_t steps =
            *rc::gen::inRange<int>(1, 65);  // 1..64 inclusive
        p.gateSteps = steps;
        p.gatePulses = *rc::gen::inRange<int>(0, steps + 1);       // 0..steps
        p.gateRotation = *rc::gen::inRange<int>(0, steps);         // 0..steps-1
        p.gateNoteLength = *rc::gen::inRange<int>(0, 6);           // 0..5
        p.gateLength = *rc::gen::inRange<int>(0, 101);             // 0..100

        // Pitch set: count in 0..MAX_SET_LEN, only [0,count) contents matter.
        const uint8_t pitchCount =
            *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);
        p.pitchCount = pitchCount;
        p.pitchOrder = *rc::gen::inRange<int>(0, 4);  // common::PlayingOrder 0..3
        for (uint8_t i = 0; i < pitchCount; i++) {
            p.pitches[i] = *rc::gen::inRange<int>(0, 128);  // arbitrary MIDI-ish byte 0..127
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
