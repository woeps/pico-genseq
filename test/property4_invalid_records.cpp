// Feature: persist-settings, Property 4: invalid records yield defaults
//
// Validates: Requirements 3.7, 5.6, 5.7, 6.1, 6.3, 6.4.
//
// Property 4 (design.md "Correctness Properties"): For any byte span that is NOT a valid
// Save_Record — wrong magic, unsupported version, a length shorter than the header, a length
// shorter than the declared payload, or an erased (all-0xFF) region — deserialization returns
// the corresponding non-OK LoadStatus and leaves the output config unchanged, so the caller
// retains its firmware defaults.
//
// The validate-in-order contract of deserialize (see SaveRecord.cpp / design "Load-path
// errors") is:
//   1. len < HEADER_SIZE                       -> TRUNCATED
//   2. magic != {'G','S'}                       -> BAD_MAGIC   (an erased all-0xFF region here)
//   3. version != FORMAT_VERSION                -> BAD_VERSION
//   4. len < HEADER_SIZE + declared payloadLen  -> TRUNCATED
//   5. crc32(payload) != stored crc             -> CRC_MISMATCH (covered by Property 3)
// This file exercises categories 1-4; each case constructs a raw span reaching exactly the
// intended failure and asserts both the LoadStatus AND that `out` (seeded to a distinct
// sentinel, as the CRC test does) is left byte-for-byte untouched.
//
// -------------------------------------------------------------------------------------------
// Shared test-runner arrangement (see test/test_main.cpp):
//
// RapidCheck's bare `rc::check` does NOT bundle a test-runner main(), and all property tests in
// this repo are swept into a SINGLE `genseq_property_tests` executable by test/CMakeLists.txt's
// `file(GLOB test/*.cpp)`. Files each defining their own main() would fail to link (duplicate
// symbol). So each property file exposes a `bool runXxx()` entry point (true = all of that
// file's properties passed) and the single shared test/test_main.cpp aggregates them under one
// main(). This file therefore defines `runPersistProperty4()` and NO main(). The name is
// distinct from the sibling midi-output-interface feature's slots.
// -------------------------------------------------------------------------------------------

#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"

#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// Little-endian u32 writer matching SaveRecord.cpp's appendU32LE / readU32LE. Used to lay down
// the payloadLength (offset 4) and crc (offset 8) fields of a raw header by hand.
void writeU32LE(std::vector<uint8_t>& buf, std::size_t offset, uint32_t v) {
    buf[offset + 0] = static_cast<uint8_t>(v & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// A distinct sentinel config used to prove `out` is left untouched by a failed deserialize.
// Recognizably "not defaults" so the untouched-check is meaningful (mirrors the CRC test).
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

// Deserialize `span` against a sentinel-seeded `out` and assert the status matches `expected`
// AND that `out` was left byte-for-byte at the sentinel (no mutation on any failure path).
void assertRejects(const std::vector<uint8_t>& span,
                   persistence::LoadStatus expected) {
    const persistence::PersistableConfig sentinel = makeSentinel();
    persistence::PersistableConfig out = sentinel;

    const persistence::LoadStatus status =
        persistence::deserialize(span.data(), span.size(), out);

    RC_ASSERT(status == expected);
    RC_ASSERT(out == sentinel);  // output config left untouched
}

// Category 1: an erased-flash region reads as all 0xFF. Its first two bytes are not the magic,
// so it is rejected as BAD_MAGIC (design maps this to firmware defaults — Req 6.1, 5.6). Length
// is >= HEADER_SIZE so validation reaches the magic check rather than the length check.
void checkErasedRegion() {
    const std::size_t len = *rc::gen::inRange<std::size_t>(
        persistence::HEADER_SIZE, persistence::HEADER_SIZE + 64u);
    const std::vector<uint8_t> span(len, 0xFFu);
    assertRejects(span, persistence::LoadStatus::BAD_MAGIC);
}

// Category 2: random bytes whose first two bytes are NOT {'G','S'}, length >= HEADER_SIZE, so
// the length check passes and the magic check fires -> BAD_MAGIC (Req 6.1).
void checkWrongMagic() {
    const std::size_t len = *rc::gen::inRange<std::size_t>(
        persistence::HEADER_SIZE, persistence::HEADER_SIZE + 64u);
    std::vector<uint8_t> span(len);
    for (std::size_t i = 0; i < len; ++i) {
        span[i] = static_cast<uint8_t>(*rc::gen::inRange<int>(0, 256));
    }
    // Force byte0/byte1 to differ from the real magic so the magic check always fails. Choosing
    // 'X','Y' is trivially != {'G','S'}, and either byte alone differing is sufficient.
    span[0] = 'X';
    span[1] = 'Y';
    assertRejects(span, persistence::LoadStatus::BAD_MAGIC);
}

// Category 3: correct magic, a version != FORMAT_VERSION, and a well-formed-enough header
// (declared payloadLength small enough that the bytes provided cover it) so validation reaches
// the version check -> BAD_VERSION (Req 6.3). Because deserialize checks version (step 3) before
// the declared-length check (step 4), the header alone is enough to reach it.
void checkBadVersion() {
    // Pick a version that is guaranteed != FORMAT_VERSION.
    int badVersion = *rc::gen::inRange<int>(0, 256);
    if (badVersion == persistence::FORMAT_VERSION) {
        badVersion = persistence::FORMAT_VERSION + 1;  // 2, still a valid u8
    }

    // Provide a small real payload and set the declared length to match, so the record would
    // pass the length check and the version check is the ONLY reason it is rejected.
    const std::size_t payloadLen =
        *rc::gen::inRange<std::size_t>(0, 8u);
    std::vector<uint8_t> span(persistence::HEADER_SIZE + payloadLen, 0u);
    span[0] = persistence::MAGIC[0];  // 'G'
    span[1] = persistence::MAGIC[1];  // 'S'
    span[2] = static_cast<uint8_t>(badVersion);
    span[3] = 0u;  // reserved
    writeU32LE(span, 4, static_cast<uint32_t>(payloadLen));  // declared payloadLength
    writeU32LE(span, 8, 0u);                                 // crc (irrelevant here)
    for (std::size_t i = 0; i < payloadLen; ++i) {
        span[persistence::HEADER_SIZE + i] =
            static_cast<uint8_t>(*rc::gen::inRange<int>(0, 256));
    }
    assertRejects(span, persistence::LoadStatus::BAD_VERSION);
}

// Category 4a: a span shorter than the fixed header (0..HEADER_SIZE-1 bytes) -> TRUNCATED,
// caught by the very first check before magic/version are even inspected (Req 6.4).
void checkSubHeaderLength() {
    const std::size_t len =
        *rc::gen::inRange<std::size_t>(0, persistence::HEADER_SIZE);  // 0..11
    std::vector<uint8_t> span(len);
    for (std::size_t i = 0; i < len; ++i) {
        span[i] = static_cast<uint8_t>(*rc::gen::inRange<int>(0, 256));
    }
    assertRejects(span, persistence::LoadStatus::TRUNCATED);
}

// Category 4b: correct magic + version, but the declared payloadLength is LARGER than the bytes
// actually provided, so the declared-length check (step 4) fires -> TRUNCATED (Req 6.4).
void checkSubPayloadLength() {
    // Actual payload bytes present after the header: 0..15.
    const std::size_t actualPayload = *rc::gen::inRange<std::size_t>(0, 16u);
    // Declared payload strictly larger than what's provided.
    const std::size_t extra = *rc::gen::inRange<std::size_t>(1u, 33u);
    const std::size_t declared = actualPayload + extra;

    std::vector<uint8_t> span(persistence::HEADER_SIZE + actualPayload, 0u);
    span[0] = persistence::MAGIC[0];  // 'G'
    span[1] = persistence::MAGIC[1];  // 'S'
    span[2] = persistence::FORMAT_VERSION;
    span[3] = 0u;  // reserved
    writeU32LE(span, 4, static_cast<uint32_t>(declared));  // over-declared length
    writeU32LE(span, 8, 0u);                               // crc (never reached)
    for (std::size_t i = 0; i < actualPayload; ++i) {
        span[persistence::HEADER_SIZE + i] =
            static_cast<uint8_t>(*rc::gen::inRange<int>(0, 256));
    }
    assertRejects(span, persistence::LoadStatus::TRUNCATED);
}

}  // namespace

// Shared test-runner entry point (see test/test_main.cpp). Exposes runPersistProperty4()
// instead of a main() so it coexists with the other property sources in genseq_property_tests.
bool runPersistProperty4() {
    // Each rc::check runs >= 100 randomized cases by default, so the five categories together
    // exercise well over 100 iterations. Every case asserts both the expected non-OK LoadStatus
    // and that the sentinel `out` is left untouched.
    bool ok = true;

    ok &= rc::check(
        "Property 4a: an all-0xFF erased region (len >= HEADER_SIZE) yields BAD_MAGIC and "
        "leaves the output config untouched",
        [] { checkErasedRegion(); });

    ok &= rc::check(
        "Property 4b: a span with wrong magic (len >= HEADER_SIZE) yields BAD_MAGIC and "
        "leaves the output config untouched",
        [] { checkWrongMagic(); });

    ok &= rc::check(
        "Property 4c: a span with correct magic but an unsupported version yields BAD_VERSION "
        "and leaves the output config untouched",
        [] { checkBadVersion(); });

    ok &= rc::check(
        "Property 4d: a span shorter than the fixed header yields TRUNCATED and leaves the "
        "output config untouched",
        [] { checkSubHeaderLength(); });

    ok &= rc::check(
        "Property 4e: a span whose declared payloadLength exceeds the bytes provided yields "
        "TRUNCATED and leaves the output config untouched",
        [] { checkSubPayloadLength(); });

    return ok;
}
