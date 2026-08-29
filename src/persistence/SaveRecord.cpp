#include "SaveRecord.h"

#include <array>

namespace persistence {

namespace {

// Little-endian appenders. The Save_Record format is little-endian throughout
// (see design "Save_Record binary layout").
void appendU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

void appendU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// Little-endian readers used by deserialize. These read from a raw span and do
// not bounds-check; the caller guards bounds via ReadCursor below.
uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// A bounds-checked forward cursor over the payload bytes. Every read is guarded
// so a count-driven payload that claims more bytes than are available can never
// read past the end; instead `ok()` goes false and the caller returns TRUNCATED
// without mutating the caller's config.
struct ReadCursor {
    const uint8_t* data;
    std::size_t len;
    std::size_t pos = 0;
    bool overrun = false;

    ReadCursor(const uint8_t* d, std::size_t l) : data(d), len(l) {}

    bool ok() const { return !overrun; }

    uint8_t u8() {
        if (pos + 1 > len) {
            overrun = true;
            return 0;
        }
        return data[pos++];
    }

    uint16_t u16LE() {
        if (pos + 2 > len) {
            overrun = true;
            return 0;
        }
        uint16_t v = static_cast<uint16_t>(data[pos]) |
                     static_cast<uint16_t>(static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2;
        return v;
    }
};

// Build the standard CRC-32 (IEEE 802.3) lookup table once, in RAM, using the
// reflected polynomial 0xEDB88320.
std::array<uint32_t, 256> makeCrc32Table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

}  // namespace

uint32_t crc32(const uint8_t* data, std::size_t len) {
    // Table built lazily and held in RAM for the process lifetime.
    static const std::array<uint32_t, 256> table = makeCrc32Table();

    uint32_t crc = 0xFFFFFFFFu;  // IEEE 802.3 initial value.
    for (std::size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;  // Final XOR.
}

std::vector<uint8_t> serialize(const PersistableConfig& cfg) {
    // Build the payload first so the CRC and payloadLength can be computed
    // before assembling the header (Req 3.3).
    std::vector<uint8_t> payload;

    appendU8(payload, cfg.patternCount);
    appendU16LE(payload, cfg.bpm);
    appendU8(payload, cfg.midiClockEnabled);

    for (uint8_t p = 0; p < cfg.patternCount && p < MAX_PATTERNS; ++p) {
        const PatternConfig& pat = cfg.patterns[p];

        appendU8(payload, pat.gateAlgorithm);
        appendU8(payload, pat.gateSteps);
        appendU8(payload, pat.gatePulses);
        appendU8(payload, pat.gateRotation);
        appendU8(payload, pat.gateNoteLength);
        appendU8(payload, pat.gateLength);

        appendU8(payload, pat.pitchCount);
        appendU8(payload, pat.pitchOrder);
        for (uint8_t i = 0; i < pat.pitchCount && i < MAX_SET_LEN; ++i) {
            appendU8(payload, pat.pitches[i]);
        }

        appendU8(payload, pat.velocityCount);
        appendU8(payload, pat.velocityOrder);
        for (uint8_t i = 0; i < pat.velocityCount && i < MAX_SET_LEN; ++i) {
            appendU8(payload, pat.velocities[i]);
        }

        appendU8(payload, pat.midiChannel);
        appendU8(payload, pat.active);
    }

    const uint32_t payloadLength = static_cast<uint32_t>(payload.size());
    const uint32_t payloadCrc = crc32(payload.data(), payload.size());

    // Assemble the 12-byte header, then append the payload.
    std::vector<uint8_t> record;
    record.reserve(HEADER_SIZE + payload.size());

    appendU8(record, MAGIC[0]);          // 'G'
    appendU8(record, MAGIC[1]);          // 'S'
    appendU8(record, FORMAT_VERSION);    // version = 1
    appendU8(record, 0u);                // reserved = 0
    appendU32LE(record, payloadLength);  // payload length (bytes)
    appendU32LE(record, payloadCrc);     // CRC-32 over payload

    record.insert(record.end(), payload.begin(), payload.end());

    return record;
}

LoadStatus deserialize(const uint8_t* bytes, std::size_t len,
                       PersistableConfig& out) {
    // Validate in strict order, returning on the first failure and NEVER
    // mutating `out` (see design "Load-path errors (validate-in-order)").

    // 1. Too short to even hold the fixed header.
    if (len < HEADER_SIZE) {
        return LoadStatus::TRUNCATED;
    }

    // 2. Magic must be {'G','S'}. An erased (all-0xFF) region fails here and is
    //    mapped to firmware defaults by the manager.
    if (bytes[0] != MAGIC[0] || bytes[1] != MAGIC[1]) {
        return LoadStatus::BAD_MAGIC;
    }

    // 3. Version must match the supported format version.
    if (bytes[2] != FORMAT_VERSION) {
        return LoadStatus::BAD_VERSION;
    }

    // 4. Declared payload length (u32 LE at offset 4) must fit in the span.
    const uint32_t payloadLength = readU32LE(bytes + 4);
    if (len < HEADER_SIZE + static_cast<std::size_t>(payloadLength)) {
        return LoadStatus::TRUNCATED;
    }

    // 5. CRC (u32 LE at offset 8) must match a fresh CRC over the payload bytes.
    const uint32_t storedCrc = readU32LE(bytes + 8);
    const uint8_t* payload = bytes + HEADER_SIZE;
    if (crc32(payload, payloadLength) != storedCrc) {
        return LoadStatus::CRC_MISMATCH;
    }

    // 6. Decode the count-driven payload into a LOCAL config first. Only after a
    //    fully successful decode is the result assigned to `out`, so `out` is
    //    never partially mutated on any failure path. All reads are bounds-
    //    guarded via ReadCursor; a payload that claims more bytes than
    //    payloadLength provides is treated as TRUNCATED.
    ReadCursor cur(payload, payloadLength);
    PersistableConfig local{};

    const uint8_t rawPatternCount = cur.u8();
    local.bpm = cur.u16LE();
    local.midiClockEnabled = cur.u8();

    // Clamp the pattern count to MAX_PATTERNS to avoid writing past the fixed
    // patterns array.
    const uint8_t patternCount =
        rawPatternCount > MAX_PATTERNS ? MAX_PATTERNS : rawPatternCount;
    local.patternCount = patternCount;

    for (uint8_t p = 0; p < patternCount; ++p) {
        PatternConfig& pat = local.patterns[p];

        pat.gateAlgorithm = cur.u8();
        pat.gateSteps = cur.u8();
        pat.gatePulses = cur.u8();
        pat.gateRotation = cur.u8();
        pat.gateNoteLength = cur.u8();
        pat.gateLength = cur.u8();

        const uint8_t rawPitchCount = cur.u8();
        pat.pitchOrder = cur.u8();
        const uint8_t pitchCount =
            rawPitchCount > MAX_SET_LEN ? MAX_SET_LEN : rawPitchCount;
        pat.pitchCount = pitchCount;
        for (uint8_t i = 0; i < pitchCount; ++i) {
            pat.pitches[i] = cur.u8();
        }

        const uint8_t rawVelocityCount = cur.u8();
        pat.velocityOrder = cur.u8();
        const uint8_t velocityCount =
            rawVelocityCount > MAX_SET_LEN ? MAX_SET_LEN : rawVelocityCount;
        pat.velocityCount = velocityCount;
        for (uint8_t i = 0; i < velocityCount; ++i) {
            pat.velocities[i] = cur.u8();
        }

        pat.midiChannel = cur.u8();
        pat.active = cur.u8();
    }

    // If any guarded read ran past the declared payload, the record is
    // truncated: leave `out` untouched.
    if (!cur.ok()) {
        return LoadStatus::TRUNCATED;
    }

    out = local;
    return LoadStatus::OK;
}

}  // namespace persistence
