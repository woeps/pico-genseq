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

    uint32_t u32LE() {
        if (pos + 4 > len) {
            overrun = true;
            return 0;
        }
        uint32_t v = static_cast<uint32_t>(data[pos]) |
                     (static_cast<uint32_t>(data[pos + 1]) << 8) |
                     (static_cast<uint32_t>(data[pos + 2]) << 16) |
                     (static_cast<uint32_t>(data[pos + 3]) << 24);
        pos += 4;
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

    // Library occupancy, then every occupied set in ascending slot order.
    appendU16LE(payload, cfg.gateOccupied);
    appendU16LE(payload, cfg.noteOccupied);
    appendU16LE(payload, cfg.velocityOccupied);

    for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
        if (!((cfg.gateOccupied >> s) & 1u)) continue;
        const GateSetRecord& g = cfg.gateSets[s];
        appendU8(payload, g.algorithm);
        appendU8(payload, g.steps);
        appendU8(payload, g.pulses);
        appendU8(payload, g.rotation);
        appendU8(payload, g.noteLength);
        appendU8(payload, g.gateLength);
        appendU8(payload, g.patternProbability);
        appendU8(payload, g.stepProbCount);
        for (uint8_t i = 0; i < g.stepProbCount && i < MAX_SET_LEN; ++i) {
            appendU8(payload, g.stepProbability[i]);
        }
        // Explicit gate array (count-driven; 0 == derive from generator on load).
        appendU8(payload, g.gateStepCount);
        for (uint8_t i = 0; i < g.gateStepCount && i < GateSetRecord::kMaxGateEditSteps; ++i) {
            appendU8(payload, g.gateStates[i]);
        }
    }
    for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
        if (!((cfg.noteOccupied >> s) & 1u)) continue;
        const NoteSetRecord& n = cfg.noteSets[s];
        appendU8(payload, n.count);
        appendU8(payload, n.order);
        for (uint8_t i = 0; i < n.count && i < MAX_SET_LEN; ++i) appendU8(payload, n.pitches[i]);
        appendU8(payload, n.scale);
        appendU8(payload, n.scaleRoot);
        appendU8(payload, n.quantize);
    }
    for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
        if (!((cfg.velocityOccupied >> s) & 1u)) continue;
        const VelocitySetRecord& v = cfg.velocitySets[s];
        appendU8(payload, v.count);
        appendU8(payload, v.order);
        for (uint8_t i = 0; i < v.count && i < MAX_SET_LEN; ++i) appendU8(payload, v.velocities[i]);
        appendU8(payload, v.humanizeRange);
        appendU8(payload, v.seedSet);
        appendU32LE(payload, v.seed);
        appendU8(payload, v.coupling);
    }

    for (uint8_t p = 0; p < cfg.patternCount && p < MAX_PATTERNS; ++p) {
        const PatternConfig& pat = cfg.patterns[p];
        appendU8(payload, pat.active);
        appendU8(payload, pat.midiChannel);
        appendU8(payload, pat.gateRef);
        appendU8(payload, pat.noteRef);
        appendU8(payload, pat.velocityRef);
    }

    const uint32_t payloadLength = static_cast<uint32_t>(payload.size());
    const uint32_t payloadCrc = crc32(payload.data(), payload.size());

    // Assemble the 12-byte header, then append the payload.
    std::vector<uint8_t> record;
    record.reserve(HEADER_SIZE + payload.size());

    appendU8(record, MAGIC[0]);          // 'G'
    appendU8(record, MAGIC[1]);          // 'S'
    appendU8(record, FORMAT_VERSION);    // version = 3
    appendU8(record, 0u);                // reserved = 0
    appendU32LE(record, payloadLength);  // payload length (bytes)
    appendU32LE(record, payloadCrc);     // CRC-32 over payload

    record.insert(record.end(), payload.begin(), payload.end());

    return record;
}

namespace {

// Decode one v1/v2 per-pattern record (material stored inline) into `pat`.
void readLegacyPattern(ReadCursor& cur, uint8_t version, LegacyPatternConfig& pat) {
    pat.gateAlgorithm = cur.u8();
    pat.gateSteps = cur.u8();
    pat.gatePulses = cur.u8();
    pat.gateRotation = cur.u8();
    pat.gateNoteLength = cur.u8();
    pat.gateLength = cur.u8();

    const uint8_t rawPitchCount = cur.u8();
    pat.pitchOrder = cur.u8();
    const uint8_t pitchCount = rawPitchCount > MAX_SET_LEN ? MAX_SET_LEN : rawPitchCount;
    pat.pitchCount = pitchCount;
    for (uint8_t i = 0; i < pitchCount; ++i) pat.pitches[i] = cur.u8();

    const uint8_t rawVelocityCount = cur.u8();
    pat.velocityOrder = cur.u8();
    const uint8_t velocityCount = rawVelocityCount > MAX_SET_LEN ? MAX_SET_LEN : rawVelocityCount;
    pat.velocityCount = velocityCount;
    for (uint8_t i = 0; i < velocityCount; ++i) pat.velocities[i] = cur.u8();

    pat.midiChannel = cur.u8();
    pat.active = cur.u8();

    // Generation Parameters block (v2 only). For v1, `pat` keeps its struct defaults
    // (behavior-preserving) and the gate array stays empty (derived from the generator).
    if (version == FORMAT_VERSION_V2_LEGACY) {
        pat.genScale = cur.u8();
        pat.genScaleRoot = cur.u8();
        pat.genQuantize = cur.u8();
        pat.genPatternProbability = cur.u8();
        pat.genHumanizeRange = cur.u8();
        pat.genSeedSet = cur.u8();
        pat.genSeed = cur.u32LE();
        pat.genCoupling = cur.u8();
        const uint8_t rawStepProbCount = cur.u8();
        const uint8_t stepProbCount =
            rawStepProbCount > MAX_SET_LEN ? MAX_SET_LEN : rawStepProbCount;
        pat.genStepProbCount = stepProbCount;
        for (uint8_t i = 0; i < stepProbCount; ++i) pat.genStepProbability[i] = cur.u8();
        const uint8_t rawGateStepCount = cur.u8();
        const uint8_t gateStepCount = rawGateStepCount > LegacyPatternConfig::kMaxGateEditSteps
                                          ? LegacyPatternConfig::kMaxGateEditSteps
                                          : rawGateStepCount;
        pat.gateStepCount = gateStepCount;
        for (uint8_t i = 0; i < gateStepCount; ++i) pat.gateStates[i] = cur.u8();
    }
}

// Migrate pattern `i` of a legacy record: it becomes its own Gate/Note/Velocity Set at slot i
// and references them (R7.4). The generation parameters split the same way the UI keeps them:
// probability with the gate set, scale with the note set, humanize/seed/coupling with the
// velocity set.
void migrateLegacyPattern(PersistableConfig& cfg, uint8_t i, const LegacyPatternConfig& lp) {
    GateSetRecord& g = cfg.gateSets[i];
    g.algorithm = lp.gateAlgorithm;
    g.steps = lp.gateSteps;
    g.pulses = lp.gatePulses;
    g.rotation = lp.gateRotation;
    g.noteLength = lp.gateNoteLength;
    g.gateLength = lp.gateLength;
    g.patternProbability = lp.genPatternProbability;
    g.stepProbCount = lp.genStepProbCount;
    for (uint8_t j = 0; j < lp.genStepProbCount && j < MAX_SET_LEN; ++j) {
        g.stepProbability[j] = lp.genStepProbability[j];
    }
    g.gateStepCount = lp.gateStepCount;
    for (uint8_t j = 0; j < lp.gateStepCount && j < GateSetRecord::kMaxGateEditSteps; ++j) {
        g.gateStates[j] = lp.gateStates[j];
    }

    NoteSetRecord& n = cfg.noteSets[i];
    n.count = lp.pitchCount;
    n.order = lp.pitchOrder;
    for (uint8_t j = 0; j < lp.pitchCount && j < MAX_SET_LEN; ++j) n.pitches[j] = lp.pitches[j];
    n.scale = lp.genScale;
    n.scaleRoot = lp.genScaleRoot;
    n.quantize = lp.genQuantize;

    VelocitySetRecord& v = cfg.velocitySets[i];
    v.count = lp.velocityCount;
    v.order = lp.velocityOrder;
    for (uint8_t j = 0; j < lp.velocityCount && j < MAX_SET_LEN; ++j) {
        v.velocities[j] = lp.velocities[j];
    }
    v.humanizeRange = lp.genHumanizeRange;
    v.seedSet = lp.genSeedSet;
    v.seed = lp.genSeed;
    v.coupling = lp.genCoupling;

    PatternConfig& p = cfg.patterns[i];
    p.active = lp.active;
    p.midiChannel = lp.midiChannel;
    p.gateRef = i;
    p.noteRef = i;
    p.velocityRef = i;
}

void readGateSet(ReadCursor& cur, GateSetRecord& g) {
    g.algorithm = cur.u8();
    g.steps = cur.u8();
    g.pulses = cur.u8();
    g.rotation = cur.u8();
    g.noteLength = cur.u8();
    g.gateLength = cur.u8();
    g.patternProbability = cur.u8();
    const uint8_t rawProbCount = cur.u8();
    g.stepProbCount = rawProbCount > MAX_SET_LEN ? MAX_SET_LEN : rawProbCount;
    for (uint8_t i = 0; i < g.stepProbCount; ++i) g.stepProbability[i] = cur.u8();
    const uint8_t rawStepCount = cur.u8();
    g.gateStepCount = rawStepCount > GateSetRecord::kMaxGateEditSteps
                          ? GateSetRecord::kMaxGateEditSteps
                          : rawStepCount;
    for (uint8_t i = 0; i < g.gateStepCount; ++i) g.gateStates[i] = cur.u8();
}

void readNoteSet(ReadCursor& cur, NoteSetRecord& n) {
    const uint8_t rawCount = cur.u8();
    n.order = cur.u8();
    n.count = rawCount > MAX_SET_LEN ? MAX_SET_LEN : rawCount;
    for (uint8_t i = 0; i < n.count; ++i) n.pitches[i] = cur.u8();
    n.scale = cur.u8();
    n.scaleRoot = cur.u8();
    n.quantize = cur.u8();
}

void readVelocitySet(ReadCursor& cur, VelocitySetRecord& v) {
    const uint8_t rawCount = cur.u8();
    v.order = cur.u8();
    v.count = rawCount > MAX_SET_LEN ? MAX_SET_LEN : rawCount;
    for (uint8_t i = 0; i < v.count; ++i) v.velocities[i] = cur.u8();
    v.humanizeRange = cur.u8();
    v.seedSet = cur.u8();
    v.seed = cur.u32LE();
    v.coupling = cur.u8();
}

bool slotPresent(uint16_t mask, uint8_t slot) {
    return slot < SET_LIBRARY_SIZE && ((mask >> slot) & 1u);
}

}  // namespace

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

    // 3. Version must be one we understand. v3 is current; v1/v2 are legacy but
    //    still loadable (migrated below). Anything else is rejected (Req 6.5).
    const uint8_t version = bytes[2];
    if (version != FORMAT_VERSION && version != FORMAT_VERSION_V2_LEGACY &&
        version != FORMAT_VERSION_V1_LEGACY) {
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
    // STACK DISCIPLINE (core1 boot-fault class): PersistableConfig is ~2.5 KB and
    // core0's whole stack is 2 KB. A stack-local staging copy overflowed core0 into
    // core1's adjacent stack on every boot load. Stage into statics instead
    // (deserialize is only ever called from the single-threaded core0 load path,
    // never re-entrantly), reset from a static default so no large temporary is
    // built on the stack.
    static const PersistableConfig kDefault{};
    static PersistableConfig local;
    static LegacyPatternConfig legacy;
    static const LegacyPatternConfig kLegacyDefault{};
    local = kDefault;

    const uint8_t rawPatternCount = cur.u8();
    local.bpm = cur.u16LE();
    local.midiClockEnabled = cur.u8();

    // Clamp the pattern count to MAX_PATTERNS to avoid writing past the fixed
    // patterns array.
    const uint8_t patternCount =
        rawPatternCount > MAX_PATTERNS ? MAX_PATTERNS : rawPatternCount;
    local.patternCount = patternCount;

    if (version == FORMAT_VERSION) {
        local.gateOccupied = cur.u16LE();
        local.noteOccupied = cur.u16LE();
        local.velocityOccupied = cur.u16LE();
        if (!cur.ok()) return LoadStatus::TRUNCATED;
        for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
            if ((local.gateOccupied >> s) & 1u) readGateSet(cur, local.gateSets[s]);
        }
        for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
            if ((local.noteOccupied >> s) & 1u) readNoteSet(cur, local.noteSets[s]);
        }
        for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
            if ((local.velocityOccupied >> s) & 1u) readVelocitySet(cur, local.velocitySets[s]);
        }
        for (uint8_t p = 0; p < patternCount; ++p) {
            PatternConfig& pat = local.patterns[p];
            pat.active = cur.u8();
            pat.midiChannel = cur.u8();
            pat.gateRef = cur.u8();
            pat.noteRef = cur.u8();
            pat.velocityRef = cur.u8();
        }
        // A payload that claims more bytes than it holds is truncated, whatever the refs say.
        if (!cur.ok()) return LoadStatus::TRUNCATED;

        // Structural validity (R7.5): every library keeps at least one set and every pattern
        // references an occupied slot of each library.
        if (local.gateOccupied == 0 || local.noteOccupied == 0 || local.velocityOccupied == 0) {
            return LoadStatus::INVALID_REFERENCE;
        }
        for (uint8_t p = 0; p < patternCount; ++p) {
            const PatternConfig& pat = local.patterns[p];
            if (!slotPresent(local.gateOccupied, pat.gateRef) ||
                !slotPresent(local.noteOccupied, pat.noteRef) ||
                !slotPresent(local.velocityOccupied, pat.velocityRef)) {
                return LoadStatus::INVALID_REFERENCE;
            }
        }
    } else {
        // v1/v2: per-pattern inline material -> pattern i owns set i of each library.
        for (uint8_t p = 0; p < patternCount; ++p) {
            legacy = kLegacyDefault;
            readLegacyPattern(cur, version, legacy);
            if (!cur.ok()) return LoadStatus::TRUNCATED;
            migrateLegacyPattern(local, p, legacy);
        }
        // Slots 0..patternCount-1 are occupied; an empty save keeps the default set 0.
        const uint16_t owned = patternCount == 0
                                   ? static_cast<uint16_t>(0x0001)
                                   : static_cast<uint16_t>((1u << patternCount) - 1u);
        local.gateOccupied = owned;
        local.noteOccupied = owned;
        local.velocityOccupied = owned;
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
