// generative-engine-expansion Property 7 (updated for format v3): legacy record migration.
//
// Validates Requirements 6.5, 1.7, 9.8 and set-library R7.3/R7.4:
//   - a v2 Save_Record (per-pattern inline material + Generation Parameters + explicit gate
//     array) still LOADS and migrates to the v3 libraries: pattern i owns gate/note/velocity
//     set i, with the generation parameters split by hosting set;
//   - a hand-built v1 (pre-milestone) record still LOADS the same way, with every set's
//     Generation Parameters at their behavior-preserving defaults and no explicit gate array
//     (gateStepCount == 0, derived from the generator as before) — a v1 save is never rejected;
//   - the migrated config re-serializes as v3 and round-trips.
//
// Shared-runner arrangement: defines runPersistProperty7() and NO main().

#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"

#include "PersistTestSupport.h"

#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// Generate a random VALID legacy (v1/v2) pattern INCLUDING Generation Parameters, so the
// v2 migration exercises the appended block.
rc::Gen<persistence::LegacyPatternConfig> genLegacyPattern() {
    return rc::gen::exec([] {
        persistence::LegacyPatternConfig p{};
        p.gateAlgorithm = 0;
        const uint8_t steps = *rc::gen::inRange<int>(1, 65);
        p.gateSteps = steps;
        p.gatePulses = *rc::gen::inRange<int>(0, steps + 1);
        p.gateRotation = *rc::gen::inRange<int>(0, steps);
        p.gateNoteLength = *rc::gen::inRange<int>(0, 6);
        p.gateLength = *rc::gen::inRange<int>(0, 101);

        const uint8_t pitchCount = *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);
        p.pitchCount = pitchCount;
        p.pitchOrder = *rc::gen::inRange<int>(0, 4);
        for (uint8_t i = 0; i < pitchCount; ++i)
            p.pitches[i] = static_cast<uint8_t>(*rc::gen::inRange<int>(0, 128));

        const uint8_t velCount = *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);
        p.velocityCount = velCount;
        p.velocityOrder = *rc::gen::inRange<int>(0, 4);
        for (uint8_t i = 0; i < velCount; ++i)
            p.velocities[i] = static_cast<uint8_t>(*rc::gen::inRange<int>(0, 128));

        p.midiChannel = *rc::gen::inRange<int>(1, 17);
        p.active = *rc::gen::inRange<int>(0, 2);

        // Generation Parameters.
        p.genScale = *rc::gen::inRange<int>(0, 3);          // CHROMATIC/MAJOR/MINOR
        p.genScaleRoot = *rc::gen::inRange<int>(0, 12);
        p.genQuantize = *rc::gen::inRange<int>(0, 4);
        p.genPatternProbability = *rc::gen::inRange<int>(0, 101);
        p.genHumanizeRange = *rc::gen::inRange<int>(0, 64);
        p.genSeedSet = *rc::gen::inRange<int>(0, 2);
        p.genSeed = *rc::gen::arbitrary<uint32_t>();
        p.genCoupling = *rc::gen::inRange<int>(0, 2);
        const uint8_t spc = *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);
        p.genStepProbCount = spc;
        for (uint8_t i = 0; i < spc; ++i) {
            // Either INHERIT (0xFF) or an explicit 0..100.
            p.genStepProbability[i] = *rc::gen::weightedOneOf<uint8_t>(
                {{1, rc::gen::just<uint8_t>(0xFF)},
                 {3, rc::gen::cast<uint8_t>(rc::gen::inRange<int>(0, 101))}});
        }
        const uint8_t gsc = *rc::gen::inRange<int>(0, persistence::LegacyPatternConfig::kMaxGateEditSteps + 1);
        p.gateStepCount = gsc;
        for (uint8_t i = 0; i < gsc; ++i)
            p.gateStates[i] = static_cast<uint8_t>(*rc::gen::inRange<int>(0, 2));
        return p;
    });
}

}  // namespace

bool runPersistProperty7() {
    bool ok = true;

    // v2 migration: every Generation Parameter and the gate array land in the right set.
    ok &= rc::check(
        "Property 7a: a v2 record migrates to per-pattern sets with Generation Parameters split by set",
        [] {
            persist_test::LegacyConfig legacy;
            legacy.patternCount = *rc::gen::inRange<int>(1, persistence::MAX_PATTERNS + 1);
            legacy.bpm = *rc::gen::inRange<int>(40, 256);
            legacy.midiClockEnabled = *rc::gen::inRange<int>(0, 2);
            for (uint8_t i = 0; i < legacy.patternCount; ++i) legacy.patterns[i] = *genLegacyPattern();

            const std::vector<uint8_t> rec =
                persist_test::serializeLegacy(legacy, persistence::FORMAT_VERSION_V2_LEGACY);
            persistence::PersistableConfig out{};
            const persistence::LoadStatus st = persistence::deserialize(rec.data(), rec.size(), out);
            RC_ASSERT(st == persistence::LoadStatus::OK);
            RC_ASSERT(out == persist_test::expectedMigration(legacy));

            // The migrated config is a valid v3 config: it re-serializes and round-trips.
            const std::vector<uint8_t> v3 = persistence::serialize(out);
            RC_ASSERT(v3[2] == persistence::FORMAT_VERSION);
            persistence::PersistableConfig again{};
            RC_ASSERT(persistence::deserialize(v3.data(), v3.size(), again) == persistence::LoadStatus::OK);
            RC_ASSERT(again == out);
        });

    // v1 migration: a legacy record loads OK, with defaults for every gen param.
    ok &= rc::check(
        "Property 7b: a v1 record migrates with Generation Parameters defaulted",
        [] {
            persist_test::LegacyConfig legacy;
            legacy.patternCount = *rc::gen::inRange<int>(1, persistence::MAX_PATTERNS + 1);
            legacy.bpm = *rc::gen::inRange<int>(40, 256);
            for (uint8_t i = 0; i < legacy.patternCount; ++i) {
                auto& p = legacy.patterns[i];
                p.gateAlgorithm = 0;
                p.gateSteps = 16; p.gatePulses = 4; p.gateRotation = 0;
                p.gateNoteLength = 1; p.gateLength = 50;
                p.pitchCount = 1; p.pitchOrder = 0; p.pitches[0] = 60;
                p.velocityCount = 1; p.velocityOrder = 0; p.velocities[0] = 100;
                p.midiChannel = 1; p.active = 1;
            }

            const std::vector<uint8_t> v1 =
                persist_test::serializeLegacy(legacy, persistence::FORMAT_VERSION_V1_LEGACY);
            persistence::PersistableConfig out{};
            const persistence::LoadStatus st = persistence::deserialize(v1.data(), v1.size(), out);
            RC_ASSERT(st == persistence::LoadStatus::OK);
            RC_ASSERT(out == persist_test::expectedMigration(legacy));
            RC_ASSERT(out.patternCount == legacy.patternCount);

            // Every set's Generation Parameters are at defaults, and no explicit gate array
            // was stored (derive-from-generator on load).
            for (uint8_t i = 0; i < out.patternCount; ++i) {
                RC_ASSERT(out.noteSets[i].scale == 0);                 // CHROMATIC
                RC_ASSERT(out.noteSets[i].scaleRoot == 0);
                RC_ASSERT(out.noteSets[i].quantize == 0);              // NEAREST
                RC_ASSERT(out.gateSets[i].patternProbability == 100);
                RC_ASSERT(out.gateSets[i].stepProbCount == 0);
                RC_ASSERT(out.gateSets[i].gateStepCount == 0);         // derive from generator
                RC_ASSERT(out.velocitySets[i].humanizeRange == 0);
                RC_ASSERT(out.velocitySets[i].seedSet == 0);
                RC_ASSERT(out.velocitySets[i].seed == 0);
                RC_ASSERT(out.velocitySets[i].coupling == 0);          // INDEPENDENT
                RC_ASSERT(out.patterns[i].gateRef == i && out.patterns[i].noteRef == i &&
                          out.patterns[i].velocityRef == i);
            }
        });

    return ok;
}
