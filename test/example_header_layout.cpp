// Feature: persist-settings, Example: Save_Record header layout (Req 3.1)
//
// Validates: Requirements 3.1.
//
// Requirement 3.1 / design "Save_Record binary layout (all little-endian)": every serialized
// Save_Record begins with a fixed 12-byte header laid out as:
//
//   Offset  Size  Field
//   0       2     magic            = {'G','S'}
//   2       1     version          = 1
//   3       1     reserved         = 0
//   4       4     payloadLength    uint32 LE   (bytes of payload only)
//   8       4     crc32            uint32 LE   (CRC-32 over payload)
//   12      ...   payload
//
// This is an EXAMPLE test (not property-based). It builds one fixed, known PersistableConfig,
// serializes it, and asserts each header field at its documented offset, including that the
// little-endian payloadLength equals record.size() - HEADER_SIZE and the little-endian crc32
// equals a fresh CRC over the payload bytes that begin at HEADER_SIZE.
//
// Host-side only: includes the SDK-free persistence codec headers plus the C++ standard
// library. It links genseq_persistence and no Pico SDK hardware libraries. It carries its own
// main(), so test/CMakeLists.txt builds it as its OWN executable and excludes it from the
// property-test glob (two main()s would fail to link).

#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// Reports one failed check and flips the shared pass flag.
void expect(bool cond, const char* what, bool& ok) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

// Reads a little-endian u32 from a byte span at the given offset.
uint32_t readU32LE(const std::vector<uint8_t>& bytes, std::size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

// Builds a fixed, known config: two patterns with small, easy-to-eyeball values.
persistence::PersistableConfig makeKnownConfig() {
    persistence::PersistableConfig cfg{};
    cfg.patternCount = 2;
    cfg.bpm = 120;
    cfg.midiClockEnabled = 1;

    persistence::PatternConfig& p0 = cfg.patterns[0];
    p0.gateAlgorithm = 1;
    p0.gateSteps = 16;
    p0.gatePulses = 4;
    p0.gateRotation = 0;
    p0.gateNoteLength = 2;
    p0.gateLength = 50;
    p0.pitchCount = 3;
    p0.pitchOrder = 0;
    p0.pitches[0] = 60;
    p0.pitches[1] = 62;
    p0.pitches[2] = 64;
    p0.velocityCount = 2;
    p0.velocityOrder = 1;
    p0.velocities[0] = 100;
    p0.velocities[1] = 110;
    p0.midiChannel = 1;
    p0.active = 1;

    persistence::PatternConfig& p1 = cfg.patterns[1];
    p1.gateAlgorithm = 0;
    p1.gateSteps = 8;
    p1.gatePulses = 2;
    p1.gateRotation = 1;
    p1.gateNoteLength = 1;
    p1.gateLength = 75;
    p1.pitchCount = 1;
    p1.pitchOrder = 0;
    p1.pitches[0] = 48;
    p1.velocityCount = 1;
    p1.velocityOrder = 0;
    p1.velocities[0] = 90;
    p1.midiChannel = 2;
    p1.active = 0;

    return cfg;
}

}  // namespace

int main() {
    using namespace persistence;

    bool ok = true;

    const PersistableConfig cfg = makeKnownConfig();
    const std::vector<uint8_t> record = serialize(cfg);

    // The record must be at least large enough to hold the fixed header.
    expect(record.size() >= HEADER_SIZE,
           "record is smaller than the fixed 12-byte header", ok);

    if (record.size() >= HEADER_SIZE) {
        // Bytes 0..1: magic {'G','S'}.
        expect(record[0] == 'G', "byte[0] is not 'G' (magic[0])", ok);
        expect(record[1] == 'S', "byte[1] is not 'S' (magic[1])", ok);

        // Byte 2: format version == 1.
        expect(record[2] == FORMAT_VERSION, "byte[2] is not FORMAT_VERSION (1)", ok);

        // Byte 3: reserved == 0.
        expect(record[3] == 0, "byte[3] (reserved) is not 0", ok);

        // Bytes 4..7: payloadLength LE == record.size() - HEADER_SIZE.
        const uint32_t expectedPayloadLength =
            static_cast<uint32_t>(record.size() - HEADER_SIZE);
        const uint32_t headerPayloadLength = readU32LE(record, 4);
        expect(headerPayloadLength == expectedPayloadLength,
               "bytes[4..7] payloadLength (LE) != record.size() - HEADER_SIZE", ok);

        // Bytes 8..11: crc32 LE == crc32 over the payload bytes.
        const std::size_t payloadLength = record.size() - HEADER_SIZE;
        const uint32_t expectedCrc =
            crc32(record.data() + HEADER_SIZE, payloadLength);
        const uint32_t headerCrc = readU32LE(record, 8);
        expect(headerCrc == expectedCrc,
               "bytes[8..11] crc32 (LE) != crc32(payload)", ok);

        // The payload begins at offset HEADER_SIZE (12): a valid record must
        // carry at least one payload byte for this known, non-empty config, and
        // the declared length must match the actual bytes after the header.
        expect(HEADER_SIZE == 12u, "HEADER_SIZE is not 12", ok);
        expect(record.size() == HEADER_SIZE + payloadLength,
               "payload does not begin at offset HEADER_SIZE", ok);
        expect(payloadLength > 0,
               "payload is empty for a known non-empty config", ok);
    }

    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: Save_Record header layout did not match the documented "
                     "12-byte little-endian layout (Req 3.1)\n");
        return 1;
    }

    std::printf("PASS: Save_Record 12-byte header layout matches the documented "
                "little-endian layout (Req 3.1)\n");
    return 0;
}
