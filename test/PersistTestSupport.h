#pragma once

// Shared helpers for the persistence tests (separate-velocity-set-gate-set-and-note-set).
//   - makeSentinel(): a recognisable, VALID v3 config used to prove a failed load leaves `out` alone.
//   - LegacyConfig / serializeLegacy(): encoder for the v1 and v2 record layouts, so migration
//     tests can build the exact bytes an older firmware wrote.
//   - expectedMigration(): the v3 config a legacy record must migrate to (pattern i owns set i).

#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"

#include <cstdint>
#include <vector>

namespace persist_test {

inline persistence::PersistableConfig makeSentinel() {
    persistence::PersistableConfig s{};
    s.patternCount = 7;
    s.bpm = 4242;
    s.midiClockEnabled = 1;
    s.gateOccupied = s.noteOccupied = s.velocityOccupied = 0x0003;
    for (uint8_t i = 0; i < 2; ++i) {
        persistence::GateSetRecord& g = s.gateSets[i];
        g.steps = 33;
        g.pulses = 11;
        g.rotation = 5;
        g.noteLength = 2;
        g.gateLength = 77;
        persistence::NoteSetRecord& n = s.noteSets[i];
        n.count = 3;
        n.order = 1;
        n.pitches[0] = 60;
        n.pitches[1] = 62;
        n.pitches[2] = 64;
        persistence::VelocitySetRecord& v = s.velocitySets[i];
        v.count = 2;
        v.order = 2;
        v.velocities[0] = 100;
        v.velocities[1] = 110;
    }
    for (uint8_t idx = 0; idx < s.patternCount; ++idx) {
        persistence::PatternConfig& pat = s.patterns[idx];
        pat.active = 1;
        pat.gateRef = idx % 2;
        pat.noteRef = (idx + 1) % 2;
        pat.velocityRef = idx % 2;
    }
    // One Instrument on channel 9 plays every pattern.
    const uint16_t all = static_cast<uint16_t>((1u << s.patternCount) - 1u);
    s.instrumentOccupied = 0x0001;
    s.instruments[0].channel = 9;
    s.instruments[0].members = s.instruments[0].enabled = all;
    return s;
}

// A v1/v2-shaped config: bpm, clock flag and per-pattern inline material.
struct LegacyConfig {
    uint8_t patternCount = 0;
    uint16_t bpm = 120;
    uint8_t midiClockEnabled = 1;
    persistence::LegacyPatternConfig patterns[persistence::MAX_PATTERNS];
};

inline void appendLE(std::vector<uint8_t>& out, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

// Encode `cfg` as a format `version` (1 or 2) record: header + CRC + per-pattern payload.
inline std::vector<uint8_t> serializeLegacy(const LegacyConfig& cfg, uint8_t version) {
    using namespace persistence;
    std::vector<uint8_t> payload;
    payload.push_back(cfg.patternCount);
    appendLE(payload, cfg.bpm, 2);
    payload.push_back(cfg.midiClockEnabled);
    for (uint8_t p = 0; p < cfg.patternCount && p < MAX_PATTERNS; ++p) {
        const LegacyPatternConfig& pat = cfg.patterns[p];
        payload.push_back(pat.gateAlgorithm);
        payload.push_back(pat.gateSteps);
        payload.push_back(pat.gatePulses);
        payload.push_back(pat.gateRotation);
        payload.push_back(pat.gateNoteLength);
        payload.push_back(pat.gateLength);
        payload.push_back(pat.pitchCount);
        payload.push_back(pat.pitchOrder);
        for (uint8_t i = 0; i < pat.pitchCount && i < MAX_SET_LEN; ++i) payload.push_back(pat.pitches[i]);
        payload.push_back(pat.velocityCount);
        payload.push_back(pat.velocityOrder);
        for (uint8_t i = 0; i < pat.velocityCount && i < MAX_SET_LEN; ++i) {
            payload.push_back(pat.velocities[i]);
        }
        payload.push_back(pat.midiChannel);
        payload.push_back(pat.active);
        if (version == FORMAT_VERSION_V2_LEGACY) {
            payload.push_back(pat.genScale);
            payload.push_back(pat.genScaleRoot);
            payload.push_back(pat.genQuantize);
            payload.push_back(pat.genPatternProbability);
            payload.push_back(pat.genHumanizeRange);
            payload.push_back(pat.genSeedSet);
            appendLE(payload, pat.genSeed, 4);
            payload.push_back(pat.genCoupling);
            payload.push_back(pat.genStepProbCount);
            for (uint8_t i = 0; i < pat.genStepProbCount && i < MAX_SET_LEN; ++i) {
                payload.push_back(pat.genStepProbability[i]);
            }
            payload.push_back(pat.gateStepCount);
            for (uint8_t i = 0; i < pat.gateStepCount && i < LegacyPatternConfig::kMaxGateEditSteps; ++i) {
                payload.push_back(pat.gateStates[i]);
            }
        }
    }
    std::vector<uint8_t> rec;
    rec.push_back(MAGIC[0]);
    rec.push_back(MAGIC[1]);
    rec.push_back(version);
    rec.push_back(0);
    appendLE(rec, static_cast<uint32_t>(payload.size()), 4);
    appendLE(rec, crc32(payload.data(), payload.size()), 4);
    rec.insert(rec.end(), payload.begin(), payload.end());
    return rec;
}

// What a legacy record must migrate to: pattern i owns gate/note/velocity set i; the generation
// parameters split by the set that hosts them (probability -> gate, scale -> note,
// humanize/seed/coupling -> velocity). A v1 record has no generation parameters, so its legacy
// struct already holds the defaults.
inline persistence::PersistableConfig expectedMigration(const LegacyConfig& legacy) {
    using namespace persistence;
    PersistableConfig cfg{};
    cfg.patternCount = legacy.patternCount;
    cfg.bpm = legacy.bpm;
    cfg.midiClockEnabled = legacy.midiClockEnabled;
    const uint16_t owned = legacy.patternCount == 0
                               ? static_cast<uint16_t>(1)
                               : static_cast<uint16_t>((1u << legacy.patternCount) - 1u);
    cfg.gateOccupied = cfg.noteOccupied = cfg.velocityOccupied = owned;
    for (uint8_t i = 0; i < legacy.patternCount; ++i) {
        const LegacyPatternConfig& lp = legacy.patterns[i];
        GateSetRecord& g = cfg.gateSets[i];
        g.algorithm = lp.gateAlgorithm;
        g.steps = lp.gateSteps;
        g.pulses = lp.gatePulses;
        g.rotation = lp.gateRotation;
        g.noteLength = lp.gateNoteLength;
        g.gateLength = lp.gateLength;
        g.patternProbability = lp.genPatternProbability;
        g.stepProbCount = lp.genStepProbCount;
        for (uint8_t j = 0; j < lp.genStepProbCount; ++j) g.stepProbability[j] = lp.genStepProbability[j];
        g.gateStepCount = lp.gateStepCount;
        for (uint8_t j = 0; j < lp.gateStepCount; ++j) g.gateStates[j] = lp.gateStates[j];
        NoteSetRecord& n = cfg.noteSets[i];
        n.count = lp.pitchCount;
        n.order = lp.pitchOrder;
        for (uint8_t j = 0; j < lp.pitchCount; ++j) n.pitches[j] = lp.pitches[j];
        n.scale = lp.genScale;
        n.scaleRoot = lp.genScaleRoot;
        n.quantize = lp.genQuantize;
        VelocitySetRecord& v = cfg.velocitySets[i];
        v.count = lp.velocityCount;
        v.order = lp.velocityOrder;
        for (uint8_t j = 0; j < lp.velocityCount; ++j) v.velocities[j] = lp.velocities[j];
        v.humanizeRange = lp.genHumanizeRange;
        v.seedSet = lp.genSeedSet;
        v.seed = lp.genSeed;
        v.coupling = lp.genCoupling;
        PatternConfig& p = cfg.patterns[i];
        p.active = lp.active;
        p.gateRef = p.noteRef = p.velocityRef = i;
    }
    // One Instrument per distinct channel (clamped to 1..16), ascending, all routes on, unmuted.
    cfg.instrumentOccupied = 0;
    uint8_t slot = 0;
    for (uint8_t ch = 1; ch <= 16; ++ch) {
        uint16_t mask = 0;
        for (uint8_t i = 0; i < legacy.patternCount; ++i) {
            const uint8_t c = legacy.patterns[i].midiChannel;
            if ((c < 1 ? 1 : (c > 16 ? 16 : c)) == ch) mask |= static_cast<uint16_t>(1u << i);
        }
        if (mask == 0) continue;
        cfg.instruments[slot].channel = ch;
        cfg.instruments[slot].members = cfg.instruments[slot].enabled = mask;
        cfg.instrumentOccupied |= static_cast<uint16_t>(1u << slot);
        ++slot;
    }
    if (cfg.instrumentOccupied == 0) cfg.instrumentOccupied = 0x0001;  // default Instrument, empty
    return cfg;
}

// A format v3 record of `cfg`: identical to v4 up to the patterns, which carry a MIDI channel
// byte (`channels[p]`) after the active flag, and no Instrument block. Built from the v4 encoder
// so the library part cannot drift.
inline std::vector<uint8_t> serializeV3(const persistence::PersistableConfig& cfg,
                                        const uint8_t* channels) {
    using namespace persistence;
    const std::vector<uint8_t> v4 = serialize(cfg);
    const std::vector<uint8_t> payload4(v4.begin() + HEADER_SIZE, v4.end());
    const std::size_t instrumentBytes =
        2 + 6 * static_cast<std::size_t>(__builtin_popcount(cfg.instrumentOccupied));
    const std::size_t prefix = payload4.size() - 4u * cfg.patternCount - instrumentBytes;
    std::vector<uint8_t> payload(payload4.begin(), payload4.begin() + prefix);
    for (uint8_t p = 0; p < cfg.patternCount; ++p) {
        payload.push_back(cfg.patterns[p].active);
        payload.push_back(channels[p]);
        payload.push_back(cfg.patterns[p].gateRef);
        payload.push_back(cfg.patterns[p].noteRef);
        payload.push_back(cfg.patterns[p].velocityRef);
    }
    std::vector<uint8_t> rec;
    rec.push_back(MAGIC[0]);
    rec.push_back(MAGIC[1]);
    rec.push_back(FORMAT_VERSION_V3_LEGACY);
    rec.push_back(0);
    appendLE(rec, static_cast<uint32_t>(payload.size()), 4);
    appendLE(rec, crc32(payload.data(), payload.size()), 4);
    rec.insert(rec.end(), payload.begin(), payload.end());
    return rec;
}

}  // namespace persist_test
