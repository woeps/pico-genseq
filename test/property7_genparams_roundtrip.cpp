// generative-engine-expansion Property 7: Generation Parameters persistence v2
// round-trip + v1 migration.
//
// Validates Requirements 6.5, 1.7, 9.8:
//   - a v2 Save_Record round-trips every Generation Parameter and the explicit
//     gate array unchanged (serialize -> deserialize -> equal);
//   - a hand-built v1 (pre-milestone) record still LOADS, with every pattern's
//     Generation Parameters at their behavior-preserving defaults and no explicit
//     gate array (gateStepCount == 0, derived from the generator as before) —
//     this is the migration contract, a v1 save is never rejected.
//
// Shared-runner arrangement: defines runPersistProperty7() and NO main().

#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"

#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// Generate a random VALID pattern config INCLUDING Generation Parameters, so the
// v2 round-trip exercises the appended block.
rc::Gen<persistence::PatternConfig> genPatternConfigV2() {
    return rc::gen::exec([] {
        persistence::PatternConfig p{};
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
        const uint8_t gsc = *rc::gen::inRange<int>(0, persistence::PatternConfig::kMaxGateEditSteps + 1);
        p.gateStepCount = gsc;
        for (uint8_t i = 0; i < gsc; ++i)
            p.gateStates[i] = static_cast<uint8_t>(*rc::gen::inRange<int>(0, 2));
        return p;
    });
}

// Build a v1 (legacy) record BY HAND: the v1 payload layout had NO Generation
// Parameters block. We serialize a v2 record, then rewrite it in the v1 shape by
// re-emitting only the v1 fields and stamping version 1. Simpler: construct the
// v1 bytes directly from a known config's v1 fields.
std::vector<uint8_t> buildV1Record(const persistence::PersistableConfig& cfg) {
    std::vector<uint8_t> payload;
    auto u8 = [&](uint8_t v) { payload.push_back(v); };
    auto u16 = [&](uint16_t v) { payload.push_back(v & 0xFF); payload.push_back((v >> 8) & 0xFF); };

    u8(cfg.patternCount);
    u16(cfg.bpm);
    u8(cfg.midiClockEnabled);
    for (uint8_t p = 0; p < cfg.patternCount && p < persistence::MAX_PATTERNS; ++p) {
        const auto& pat = cfg.patterns[p];
        u8(pat.gateAlgorithm); u8(pat.gateSteps); u8(pat.gatePulses);
        u8(pat.gateRotation); u8(pat.gateNoteLength); u8(pat.gateLength);
        u8(pat.pitchCount); u8(pat.pitchOrder);
        for (uint8_t i = 0; i < pat.pitchCount && i < persistence::MAX_SET_LEN; ++i) u8(pat.pitches[i]);
        u8(pat.velocityCount); u8(pat.velocityOrder);
        for (uint8_t i = 0; i < pat.velocityCount && i < persistence::MAX_SET_LEN; ++i) u8(pat.velocities[i]);
        u8(pat.midiChannel); u8(pat.active);
        // NOTE: no Generation Parameters block — this is the v1 shape.
    }

    std::vector<uint8_t> rec;
    rec.push_back('G'); rec.push_back('S');
    rec.push_back(persistence::FORMAT_VERSION_V1_LEGACY);  // version 1
    rec.push_back(0);
    const uint32_t len = static_cast<uint32_t>(payload.size());
    rec.push_back(len & 0xFF); rec.push_back((len >> 8) & 0xFF);
    rec.push_back((len >> 16) & 0xFF); rec.push_back((len >> 24) & 0xFF);
    const uint32_t crc = persistence::crc32(payload.data(), payload.size());
    rec.push_back(crc & 0xFF); rec.push_back((crc >> 8) & 0xFF);
    rec.push_back((crc >> 16) & 0xFF); rec.push_back((crc >> 24) & 0xFF);
    rec.insert(rec.end(), payload.begin(), payload.end());
    return rec;
}

}  // namespace

bool runPersistProperty7() {
    bool ok = true;

    // v2 round-trip: every Generation Parameter and the gate array survive.
    ok &= rc::check(
        "Property 7a: v2 Save_Record round-trips Generation Parameters + gate array",
        [] {
            persistence::PersistableConfig cfg{};
            const uint8_t count = *rc::gen::inRange<int>(1, persistence::MAX_PATTERNS + 1);
            cfg.patternCount = count;
            cfg.bpm = *rc::gen::inRange<int>(40, 256);
            cfg.midiClockEnabled = *rc::gen::inRange<int>(0, 2);
            for (uint8_t i = 0; i < count; ++i) cfg.patterns[i] = *genPatternConfigV2();

            const std::vector<uint8_t> rec = persistence::serialize(cfg);
            persistence::PersistableConfig out{};
            const persistence::LoadStatus st =
                persistence::deserialize(rec.data(), rec.size(), out);
            RC_ASSERT(st == persistence::LoadStatus::OK);
            RC_ASSERT(out == cfg);
        });

    // v1 migration: a legacy record loads OK, with defaults for every gen param.
    ok &= rc::check(
        "Property 7b: a v1 record loads with Generation Parameters defaulted",
        [] {
            persistence::PersistableConfig cfg{};
            const uint8_t count = *rc::gen::inRange<int>(1, persistence::MAX_PATTERNS + 1);
            cfg.patternCount = count;
            cfg.bpm = *rc::gen::inRange<int>(40, 256);
            cfg.midiClockEnabled = 1;
            for (uint8_t i = 0; i < count; ++i) {
                auto& p = cfg.patterns[i];
                p.gateAlgorithm = 0;
                p.gateSteps = 16; p.gatePulses = 4; p.gateRotation = 0;
                p.gateNoteLength = 1; p.gateLength = 50;
                p.pitchCount = 1; p.pitchOrder = 0; p.pitches[0] = 60;
                p.velocityCount = 1; p.velocityOrder = 0; p.velocities[0] = 100;
                p.midiChannel = 1; p.active = 1;
            }

            const std::vector<uint8_t> v1 = buildV1Record(cfg);
            persistence::PersistableConfig out{};
            const persistence::LoadStatus st =
                persistence::deserialize(v1.data(), v1.size(), out);
            RC_ASSERT(st == persistence::LoadStatus::OK);
            RC_ASSERT(out.patternCount == cfg.patternCount);

            // Every pattern's Generation Parameters are at defaults, and no
            // explicit gate array was stored (derive-from-generator on load).
            for (uint8_t i = 0; i < out.patternCount; ++i) {
                const auto& p = out.patterns[i];
                RC_ASSERT(p.genScale == 0);                 // CHROMATIC
                RC_ASSERT(p.genScaleRoot == 0);
                RC_ASSERT(p.genQuantize == 0);              // NEAREST
                RC_ASSERT(p.genPatternProbability == 100);
                RC_ASSERT(p.genHumanizeRange == 0);
                RC_ASSERT(p.genSeedSet == 0);
                RC_ASSERT(p.genSeed == 0);
                RC_ASSERT(p.genCoupling == 0);              // INDEPENDENT
                RC_ASSERT(p.genStepProbCount == 0);
                RC_ASSERT(p.gateStepCount == 0);            // derive from generator
            }
        });

    return ok;
}
