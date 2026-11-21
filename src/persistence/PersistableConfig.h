#pragma once

#include <cstdint>

#include "../common/pitch_set.h"  // common::PlayingOrder

namespace persistence {

// Mirrors ui::state::MAX_PATTERNS (max supported pattern count).
constexpr uint8_t MAX_PATTERNS = 15;
// Mirrors ui::state MAX_PITCHES / MAX_VELOCITIES (max set length).
constexpr uint8_t MAX_SET_LEN = 16;

// Format v1/v2 per-pattern record (material stored inline per pattern). DECODE-ONLY: the
// codec reads it and migrates it into the v3 set libraries (one set of each type per
// pattern); nothing writes it any more except legacy-record test helpers.
struct LegacyPatternConfig {
    // Gate set: persisted as the euclidean generator parameters (the source of
    // truth in UIState gate sets), not the expanded boolean vector.
    uint8_t gateAlgorithm;   // ui::state::GateAlgorithm
    uint8_t gateSteps;       // 1..64
    uint8_t gatePulses;      // 0..steps
    uint8_t gateRotation;    // 0..steps-1
    uint8_t gateNoteLength;  // ui::state::NoteLength enum
    uint8_t gateLength;      // 0..100

    uint8_t pitchCount;              // 0..MAX_SET_LEN
    uint8_t pitchOrder;              // common::PlayingOrder
    uint8_t pitches[MAX_SET_LEN];    // only [0, pitchCount) meaningful

    uint8_t velocityCount;               // 0..MAX_SET_LEN
    uint8_t velocityOrder;               // common::PlayingOrder
    uint8_t velocities[MAX_SET_LEN];     // only [0, velocityCount) meaningful

    uint8_t midiChannel;  // 1..16 (reserved; defaulted from convention today)
    uint8_t active;       // 0/1 (from UIState::activePatterns bit)

    // --- Generation Parameters (generative-engine-expansion, format v2) ------
    // Behavior-preserving defaults so a v1 record decodes to today's behavior.
    uint8_t genScale = 0;            // common::ScaleId (0 == CHROMATIC)
    uint8_t genScaleRoot = 0;        // 0..11
    uint8_t genQuantize = 0;         // common::QuantizeMode (0 == NEAREST)
    uint8_t genPatternProbability = 100;  // 0..100
    uint8_t genHumanizeRange = 0;    // +/- amount
    uint8_t genSeedSet = 0;          // 0/1
    uint32_t genSeed = 0;
    uint8_t genCoupling = 0;         // common::CouplingMode (0 == INDEPENDENT)
    // Per-step probability, count-driven. 0xFF == INHERIT.
    uint8_t genStepProbCount = 0;    // 0..MAX_SET_LEN
    uint8_t genStepProbability[MAX_SET_LEN];  // only [0, genStepProbCount) meaningful

    // Explicit gate array (format v2): manual per-step gate editing (R9.2) can
    // diverge from what the generator parameters above would produce, so the
    // actual gate states are persisted. On a v1 load this is empty and the gate
    // array is derived from the generator params as before. Up to 64 UI steps.
    static constexpr uint8_t kMaxGateEditSteps = 64;
    uint8_t gateStepCount = 0;                       // 0 == derive from generator
    uint8_t gateStates[kMaxGateEditSteps] = {0};     // 0/1 per step
};

// Compares only the meaningful elements (respecting counts), matching the
// round-trip guarantee described by the design's Property 1.
inline bool operator==(const LegacyPatternConfig& lhs, const LegacyPatternConfig& rhs) {
    if (lhs.gateAlgorithm != rhs.gateAlgorithm ||
        lhs.gateSteps != rhs.gateSteps || lhs.gatePulses != rhs.gatePulses ||
        lhs.gateRotation != rhs.gateRotation ||
        lhs.gateNoteLength != rhs.gateNoteLength ||
        lhs.gateLength != rhs.gateLength) {
        return false;
    }
    if (lhs.pitchCount != rhs.pitchCount || lhs.pitchOrder != rhs.pitchOrder) {
        return false;
    }
    for (uint8_t i = 0; i < lhs.pitchCount && i < MAX_SET_LEN; i++) {
        if (lhs.pitches[i] != rhs.pitches[i]) return false;
    }
    if (lhs.velocityCount != rhs.velocityCount ||
        lhs.velocityOrder != rhs.velocityOrder) {
        return false;
    }
    for (uint8_t i = 0; i < lhs.velocityCount && i < MAX_SET_LEN; i++) {
        if (lhs.velocities[i] != rhs.velocities[i]) return false;
    }
    if (lhs.midiChannel != rhs.midiChannel || lhs.active != rhs.active) return false;

    // Generation Parameters (format v2).
    if (lhs.genScale != rhs.genScale || lhs.genScaleRoot != rhs.genScaleRoot ||
        lhs.genQuantize != rhs.genQuantize ||
        lhs.genPatternProbability != rhs.genPatternProbability ||
        lhs.genHumanizeRange != rhs.genHumanizeRange ||
        lhs.genSeedSet != rhs.genSeedSet || lhs.genSeed != rhs.genSeed ||
        lhs.genCoupling != rhs.genCoupling ||
        lhs.genStepProbCount != rhs.genStepProbCount) {
        return false;
    }
    for (uint8_t i = 0; i < lhs.genStepProbCount && i < MAX_SET_LEN; i++) {
        if (lhs.genStepProbability[i] != rhs.genStepProbability[i]) return false;
    }
    if (lhs.gateStepCount != rhs.gateStepCount) return false;
    for (uint8_t i = 0; i < lhs.gateStepCount && i < LegacyPatternConfig::kMaxGateEditSteps; i++) {
        if (lhs.gateStates[i] != rhs.gateStates[i]) return false;
    }
    return true;
}

inline bool operator!=(const LegacyPatternConfig& lhs, const LegacyPatternConfig& rhs) {
    return !(lhs == rhs);
}

// --- Format v3: set libraries + per-pattern references --------------------------------
// (separate-velocity-set-gate-set-and-note-set). Mirrors ui::state::SET_LIBRARY_SIZE.
constexpr uint8_t SET_LIBRARY_SIZE = 16;

// One Gate Set (rhythm material + the generation parameters that live with it).
struct GateSetRecord {
    static constexpr uint8_t kMaxGateEditSteps = 64;
    uint8_t algorithm = 0;     // ui::state::GateAlgorithm
    uint8_t steps = 16;        // 1..64
    uint8_t pulses = 4;        // 0..steps
    uint8_t rotation = 0;      // 0..steps-1
    uint8_t noteLength = 1;    // ui::state::NoteLength
    uint8_t gateLength = 50;   // 0..100
    uint8_t patternProbability = 100;  // 0..100
    uint8_t stepProbCount = 0;         // 0..MAX_SET_LEN
    uint8_t stepProbability[MAX_SET_LEN] = {0};  // 0xFF == INHERIT; [0, stepProbCount) meaningful
    uint8_t gateStepCount = 0;                   // 0 == derive from the generator
    uint8_t gateStates[kMaxGateEditSteps] = {0}; // 0/1 per step; [0, gateStepCount) meaningful
};

// One Note Set (pitches + the scale constraint that lives with it).
struct NoteSetRecord {
    uint8_t count = 4;                    // 0..MAX_SET_LEN
    uint8_t order = 0;                    // common::PlayingOrder
    uint8_t pitches[MAX_SET_LEN] = {60, 67, 69, 72};  // [0, count) meaningful
    uint8_t scale = 0;                    // common::ScaleId (0 == CHROMATIC)
    uint8_t scaleRoot = 0;                // 0..11
    uint8_t quantize = 0;                 // common::QuantizeMode (0 == NEAREST)
};

// One Velocity Set (velocities + humanize/seed/coupling that live with it).
struct VelocitySetRecord {
    uint8_t count = 4;                    // 0..MAX_SET_LEN
    uint8_t order = 0;                    // common::PlayingOrder
    uint8_t velocities[MAX_SET_LEN] = {100, 80, 60, 80};  // [0, count) meaningful
    uint8_t humanizeRange = 0;
    uint8_t seedSet = 0;                  // 0/1
    uint32_t seed = 0;
    uint8_t coupling = 0;                 // common::CouplingMode (0 == INDEPENDENT)
};

// Mirrors ui::state::MAX_INSTRUMENTS (one Instrument per MIDI channel).
constexpr uint8_t MAX_INSTRUMENTS = 16;

// One Instrument: a MIDI channel, a mute flag and two pattern masks (bit p = pattern p).
// `enabled` is the per-route switch; it may only have bits that are also set in `members`.
struct InstrumentRecord {
    uint8_t channel = 1;     // 1..16, unique among occupied Instruments
    uint8_t muted = 0;       // 0/1
    uint16_t members = 0;    // patterns assigned to the Instrument
    uint16_t enabled = 0;    // routes switched on (subset of members)
};

// Durable per-pattern configuration: flags plus one reference per library. The MIDI channel
// is no longer stored per pattern: it comes from the Instruments (format v4).
struct PatternConfig {
    uint8_t active = 0;       // 0/1 (from UIState::activePatterns bit)
    uint8_t gateRef = 0;      // slot in gateSets, must be occupied
    uint8_t noteRef = 0;      // slot in noteSets, must be occupied
    uint8_t velocityRef = 0;  // slot in velocitySets, must be occupied
};

inline bool operator==(const GateSetRecord& a, const GateSetRecord& b) {
    if (a.algorithm != b.algorithm || a.steps != b.steps || a.pulses != b.pulses ||
        a.rotation != b.rotation || a.noteLength != b.noteLength ||
        a.gateLength != b.gateLength || a.patternProbability != b.patternProbability ||
        a.stepProbCount != b.stepProbCount || a.gateStepCount != b.gateStepCount) {
        return false;
    }
    for (uint8_t i = 0; i < a.stepProbCount && i < MAX_SET_LEN; i++) {
        if (a.stepProbability[i] != b.stepProbability[i]) return false;
    }
    for (uint8_t i = 0; i < a.gateStepCount && i < GateSetRecord::kMaxGateEditSteps; i++) {
        if (a.gateStates[i] != b.gateStates[i]) return false;
    }
    return true;
}
inline bool operator!=(const GateSetRecord& a, const GateSetRecord& b) { return !(a == b); }

inline bool operator==(const NoteSetRecord& a, const NoteSetRecord& b) {
    if (a.count != b.count || a.order != b.order || a.scale != b.scale ||
        a.scaleRoot != b.scaleRoot || a.quantize != b.quantize) {
        return false;
    }
    for (uint8_t i = 0; i < a.count && i < MAX_SET_LEN; i++) {
        if (a.pitches[i] != b.pitches[i]) return false;
    }
    return true;
}
inline bool operator!=(const NoteSetRecord& a, const NoteSetRecord& b) { return !(a == b); }

inline bool operator==(const VelocitySetRecord& a, const VelocitySetRecord& b) {
    if (a.count != b.count || a.order != b.order || a.humanizeRange != b.humanizeRange ||
        a.seedSet != b.seedSet || a.seed != b.seed || a.coupling != b.coupling) {
        return false;
    }
    for (uint8_t i = 0; i < a.count && i < MAX_SET_LEN; i++) {
        if (a.velocities[i] != b.velocities[i]) return false;
    }
    return true;
}
inline bool operator!=(const VelocitySetRecord& a, const VelocitySetRecord& b) { return !(a == b); }

inline bool operator==(const PatternConfig& a, const PatternConfig& b) {
    return a.active == b.active && a.gateRef == b.gateRef && a.noteRef == b.noteRef &&
           a.velocityRef == b.velocityRef;
}
inline bool operator==(const InstrumentRecord& a, const InstrumentRecord& b) {
    return a.channel == b.channel && a.muted == b.muted && a.members == b.members &&
           a.enabled == b.enabled;
}
inline bool operator!=(const InstrumentRecord& a, const InstrumentRecord& b) { return !(a == b); }
inline bool operator!=(const PatternConfig& a, const PatternConfig& b) { return !(a == b); }

// The in-memory snapshot the codec serializes. A plain aggregate derived from UIState,
// holding only durable fields. Occupancy masks default to slot 0 so a minimal config is valid.
struct PersistableConfig {
    uint8_t patternCount;   // 0..MAX_PATTERNS
    uint16_t bpm;           // sequencer-level
    uint8_t midiClockEnabled;  // 0/1 (reserved; defaults to 1 today)
    uint16_t gateOccupied = 0x0001;      // bit s set == gateSets[s] is present
    uint16_t noteOccupied = 0x0001;
    uint16_t velocityOccupied = 0x0001;
    GateSetRecord gateSets[SET_LIBRARY_SIZE];
    NoteSetRecord noteSets[SET_LIBRARY_SIZE];
    VelocitySetRecord velocitySets[SET_LIBRARY_SIZE];
    PatternConfig patterns[MAX_PATTERNS];
    uint16_t instrumentOccupied = 0x0001;  // bit s set == instruments[s] is present
    InstrumentRecord instruments[MAX_INSTRUMENTS];
};

// Compares only the meaningful content: occupied slots and [0, patternCount) patterns.
inline bool operator==(const PersistableConfig& lhs, const PersistableConfig& rhs) {
    if (lhs.patternCount != rhs.patternCount || lhs.bpm != rhs.bpm ||
        lhs.midiClockEnabled != rhs.midiClockEnabled ||
        lhs.gateOccupied != rhs.gateOccupied || lhs.noteOccupied != rhs.noteOccupied ||
        lhs.velocityOccupied != rhs.velocityOccupied) {
        return false;
    }
    for (uint8_t i = 0; i < SET_LIBRARY_SIZE; i++) {
        if ((lhs.gateOccupied >> i) & 1u) {
            if (lhs.gateSets[i] != rhs.gateSets[i]) return false;
        }
        if ((lhs.noteOccupied >> i) & 1u) {
            if (lhs.noteSets[i] != rhs.noteSets[i]) return false;
        }
        if ((lhs.velocityOccupied >> i) & 1u) {
            if (lhs.velocitySets[i] != rhs.velocitySets[i]) return false;
        }
    }
    for (uint8_t i = 0; i < lhs.patternCount && i < MAX_PATTERNS; i++) {
        if (lhs.patterns[i] != rhs.patterns[i]) return false;
    }
    if (lhs.instrumentOccupied != rhs.instrumentOccupied) return false;
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; i++) {
        if (((lhs.instrumentOccupied >> i) & 1u) && lhs.instruments[i] != rhs.instruments[i]) {
            return false;
        }
    }
    return true;
}

inline bool operator!=(const PersistableConfig& lhs, const PersistableConfig& rhs) {
    return !(lhs == rhs);
}

} // namespace persistence
