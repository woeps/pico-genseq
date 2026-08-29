// Feature: persist-settings, Property 3: CRC detects corruption
//
// Validates: Requirements 3.6, 6.2.
//
// Property 3 (design.md "Correctness Properties"): For any valid Save_Record and any mutation
// of its payload bytes that changes the payload, deserialization recomputes a CRC that does
// not match the stored CRC, returns a CRC-mismatch load failure, and does not populate the
// output config.
//
// The CRC (stored in the header at offset 8..11) is computed over the payload bytes ONLY and
// deserialize validates it BEFORE decoding the payload. So flipping ANY payload byte
// (index >= HEADER_SIZE) to a different value flips the recomputed CRC and yields
// LoadStatus::CRC_MISMATCH — and because the CRC check precedes decode, `out` is left
// untouched. A valid record always has >= 4 payload bytes (the fixed prefix patternCount(1) +
// bpm(2) + midiClockEnabled(1)) even when patternCount == 0, so a payload byte to mutate
// always exists.
//
// -------------------------------------------------------------------------------------------
// Shared test-runner arrangement (see test/test_main.cpp):
//
// RapidCheck's bare `rc::check` does NOT bundle a test-runner main(), and all property tests in
// this repo are swept into a SINGLE `genseq_property_tests` executable by test/CMakeLists.txt's
// `file(GLOB test/*.cpp)`. Files each defining their own main() would fail to link (duplicate
// symbol). So each property file exposes a `bool runXxx()` entry point (true = all of that
// file's properties passed) and the single shared test/test_main.cpp aggregates them under one
// main(). This file therefore defines `runPersistProperty3()` and NO main(). The name is
// distinct from the sibling midi-output-interface feature's `runProperty3()` slot.
// -------------------------------------------------------------------------------------------

#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"

#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// Generate one random VALID persistence::PatternConfig. Every field is drawn inside the range
// the schema documents (see PersistableConfig.h); only [0, count) set elements are filled.
rc::Gen<persistence::PatternConfig> genPatternConfig() {
    return rc::gen::exec([] {
        persistence::PatternConfig p{};

        const uint8_t steps = *rc::gen::inRange<int>(1, 65);  // 1..64
        p.gateAlgorithm = 0;
        p.gateSteps = steps;
        p.gatePulses = *rc::gen::inRange<int>(0, steps + 1);  // 0..steps
        p.gateRotation = *rc::gen::inRange<int>(0, steps);    // 0..steps-1
        p.gateNoteLength = *rc::gen::inRange<int>(0, 6);      // 0..5
        p.gateLength = *rc::gen::inRange<int>(0, 101);        // 0..100

        const uint8_t pitchCount =
            *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);  // 0..16
        p.pitchCount = pitchCount;
        p.pitchOrder = *rc::gen::inRange<int>(0, 4);
        for (uint8_t i = 0; i < pitchCount; i++) {
            p.pitches[i] = *rc::gen::inRange<int>(0, 128);  // 0..127
        }

        const uint8_t velocityCount =
            *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);  // 0..16
        p.velocityCount = velocityCount;
        p.velocityOrder = *rc::gen::inRange<int>(0, 4);
        for (uint8_t i = 0; i < velocityCount; i++) {
            p.velocities[i] = *rc::gen::inRange<int>(0, 128);  // 0..127
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
        cfg.bpm = *rc::gen::inRange<int>(0, 1001);            // 0..1000
        cfg.midiClockEnabled = *rc::gen::inRange<int>(0, 2);  // 0/1
        for (uint8_t i = 0; i < patternCount; i++) {
            cfg.patterns[i] = *genPatternConfig();
        }
        return cfg;
    });
}

// A distinct sentinel config used to prove `out` is left untouched by a failed deserialize.
// Recognizably "not defaults" so an untouched-check is meaningful.
persistence::PersistableConfig makeSentinel() {
    persistence::PersistableConfig s{};
    s.patternCount = 7;
    s.bpm = 4242;
    s.midiClockEnabled = 1;
    for (uint8_t idx = 0; idx < s.patternCount; ++idx) {
        persistence::PatternConfig& pat = s.patterns[idx];
        pat.gateAlgorithm = 0;
        pat.gateSteps = 33;
        pat.gatePulses = 11;
        pat.gateRotation = 5;
        pat.gateNoteLength = 2;
        pat.gateLength = 77;
        pat.pitchCount = 3;
        pat.pitchOrder = 1;
        pat.pitches[0] = 60;
        pat.pitches[1] = 62;
        pat.pitches[2] = 64;
        pat.velocityCount = 2;
        pat.velocityOrder = 2;
        pat.velocities[0] = 100;
        pat.velocities[1] = 110;
        pat.midiChannel = 9;
        pat.active = 1;
    }
    return s;
}

}  // namespace

// Shared test-runner entry point (see test/test_main.cpp). Exposes runPersistProperty3()
// instead of a main() so it coexists with the other property sources in genseq_property_tests.
bool runPersistProperty3() {
    // For any valid config, serialize it, mutate ONE payload byte (index >= HEADER_SIZE) to a
    // genuinely different value, and assert deserialize reports CRC_MISMATCH and leaves the
    // caller's `out` byte-for-byte at its sentinel. RapidCheck runs >=100 cases by default.
    return rc::check(
        "Property 3: any single-byte payload mutation of a valid Save_Record yields "
        "CRC_MISMATCH and leaves the output config untouched",
        [] {
            const persistence::PersistableConfig cfg = *genPersistableConfig();

            std::vector<uint8_t> record = persistence::serialize(cfg);

            // A valid record always has the 12-byte header plus a >= 4-byte fixed payload
            // prefix (patternCount + bpm + midiClockEnabled), so a payload byte always exists.
            RC_ASSERT(record.size() > persistence::HEADER_SIZE);

            const std::size_t payloadLen = record.size() - persistence::HEADER_SIZE;

            // Pick a random PAYLOAD byte index in [HEADER_SIZE, record.size()).
            const std::size_t payloadOffset =
                *rc::gen::inRange<std::size_t>(0, payloadLen);
            const std::size_t index = persistence::HEADER_SIZE + payloadOffset;

            // Change it to a DIFFERENT value. XOR 0xFF flips every bit, so the byte always
            // truly changes and the payload is genuinely corrupted.
            record[index] = static_cast<uint8_t>(record[index] ^ 0xFFu);

            // Seed `out` with a distinct sentinel; a failed deserialize must not touch it.
            const persistence::PersistableConfig sentinel = makeSentinel();
            persistence::PersistableConfig out = sentinel;

            const persistence::LoadStatus status =
                persistence::deserialize(record.data(), record.size(), out);

            RC_ASSERT(status == persistence::LoadStatus::CRC_MISMATCH);
            RC_ASSERT(out == sentinel);  // output config left untouched
        });
}
