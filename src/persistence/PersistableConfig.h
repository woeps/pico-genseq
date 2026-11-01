#pragma once

#include <cstdint>

#include "../common/pitch_set.h"  // common::PlayingOrder

namespace persistence {

// Mirrors ui::state::MAX_PATTERNS (max supported pattern count).
constexpr uint8_t MAX_PATTERNS = 15;
// Mirrors ui::state MAX_PITCHES / MAX_VELOCITIES (max set length).
constexpr uint8_t MAX_SET_LEN = 16;

// Durable per-pattern configuration. Holds only persistable fields derived
// from ui::state::UIState — no transient runtime state (position / randomState).
struct PatternConfig {
    // Gate set: persisted as the euclidean generator parameters (the source of
    // truth in UIState::gateSetConfigs), not the expanded boolean vector.
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
inline bool operator==(const PatternConfig& lhs, const PatternConfig& rhs) {
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
    for (uint8_t i = 0; i < lhs.gateStepCount && i < PatternConfig::kMaxGateEditSteps; i++) {
        if (lhs.gateStates[i] != rhs.gateStates[i]) return false;
    }
    return true;
}

inline bool operator!=(const PatternConfig& lhs, const PatternConfig& rhs) {
    return !(lhs == rhs);
}

// The in-memory snapshot the codec serializes. A plain aggregate derived from
// UIState, holding only durable fields.
struct PersistableConfig {
    uint8_t patternCount;   // 0..MAX_PATTERNS
    uint16_t bpm;           // sequencer-level
    uint8_t midiClockEnabled;  // 0/1 (reserved; defaults to 1 today)
    PatternConfig patterns[MAX_PATTERNS];
};

// Compares only the meaningful patterns (respecting patternCount).
inline bool operator==(const PersistableConfig& lhs,
                       const PersistableConfig& rhs) {
    if (lhs.patternCount != rhs.patternCount || lhs.bpm != rhs.bpm ||
        lhs.midiClockEnabled != rhs.midiClockEnabled) {
        return false;
    }
    for (uint8_t i = 0; i < lhs.patternCount && i < MAX_PATTERNS; i++) {
        if (lhs.patterns[i] != rhs.patterns[i]) return false;
    }
    return true;
}

inline bool operator!=(const PersistableConfig& lhs,
                       const PersistableConfig& rhs) {
    return !(lhs == rhs);
}

} // namespace persistence
