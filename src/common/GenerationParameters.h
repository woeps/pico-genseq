#pragma once

// generative-engine-expansion task 1.1 — the per-Pattern GenerationParameters
// value object.
//
// This is the single value object that bounds and shapes what the existing
// PitchSet / VelocitySet / GateSet streams emit, WITHOUT rewriting any stream.
// Every field defaults to a behavior-preserving value, so a default-constructed
// GenerationParameters makes the sequencer emit output byte-identical to
// pre-milestone GenSeq (design invariant 1). The emission lens
// (Sequencer::emitStep) short-circuits on isDefault() to the exact current
// note-on call, so the default path adds near-zero Hot Path cost.
//
// POD + fixed-size + SDK-free on purpose (design §1): it must live in both
// core0 Working State and the core1 PatternSnapshot (a heap-free POD carrier),
// and compile identically in firmware and host tests. No std::vector, no heap,
// only <cstdint>.

#include <cstdint>

namespace common {

// Musical scale selector. Stored as an id (not an expanded mask) so new scales
// can be added without bumping the persistence format version (Req 1.6); the
// runtime expands (id, root) into a 12-bit pitch-class membership mask once per
// parameter change (design §2/§5.2). CHROMATIC == no constraint == default.
enum class ScaleId : uint8_t {
    CHROMATIC = 0,  // unconstrained — default, every pitch class allowed
    MAJOR,
    MINOR_NATURAL,
    // ... extensible: append new scales here without a format bump.
};

// Policy for resolving an out-of-scale candidate pitch (Req 1.3).
enum class QuantizeMode : uint8_t {
    NEAREST = 0,  // default — snap to the nearest allowed pitch
    DOWN,         // snap down to the nearest allowed pitch at or below
    UP,           // snap up to the nearest allowed pitch at or above
    SKIP,         // suppress the step entirely (Req 1.4)
};

// Stream coupling mode (Req 5.2). INDEPENDENT == current behavior == default.
enum class CouplingMode : uint8_t {
    INDEPENDENT = 0,  // default — every stream advances on its own position
    LOCKED,           // velocity index follows the pitch stream position
};

// Per-step probability sentinel: a step set to INHERIT falls back to the
// per-Pattern probability (Req 2.4 / 9.5). This keeps "inherit" DISTINCT from an
// explicit 100 percent in a single byte, with no parallel is-set array. Explicit
// values are 0..100 (an integer percent).
static constexpr uint8_t STEP_PROB_INHERIT = 0xFF;

struct GenerationParameters {
    // Per-step probability is a property of a PROGRAMMED step, so it is bounded
    // by the editable step count, NOT by the raw per-tick gate cap. 16 matches
    // the UI step/pitch granularity and keeps the struct small.
    static constexpr uint8_t kMaxSteps = 16;

    // --- Scale constraint (Req 1) ---
    ScaleId scale = ScaleId::CHROMATIC;            // default: no constraint
    uint8_t scaleRoot = 0;                         // pitch class 0..11 (0 == C)
    QuantizeMode quantize = QuantizeMode::NEAREST;

    // --- Step probability (Req 2) ---
    uint8_t patternProbability = 100;              // 0..100 percent; default 100
    uint8_t stepProbability[kMaxSteps];            // per-step override or INHERIT

    // --- Velocity humanization (Req 3) ---
    uint8_t humanizeRange = 0;                     // +/- amount; default 0

    // --- Determinism (Req 4) ---
    bool seedSet = false;                          // default: free-running
    uint32_t seed = 0;

    // --- Coupling (Req 5) ---
    CouplingMode coupling = CouplingMode::INDEPENDENT;  // default

    GenerationParameters() {
        for (uint8_t i = 0; i < kMaxSteps; ++i) {
            stepProbability[i] = STEP_PROB_INHERIT;
        }
    }

    // True only when EVERY field is at its behavior-preserving default. The
    // emission lens short-circuits on this so the no-feature path is provably
    // identical to pre-milestone output and O(1) (design invariant 1 + 3).
    bool isDefault() const {
        if (scale != ScaleId::CHROMATIC) return false;
        // scaleRoot is inert while CHROMATIC (no constraint applies), so it does
        // not affect default-equivalence; included for completeness/clarity.
        if (scaleRoot != 0) return false;
        if (quantize != QuantizeMode::NEAREST) return false;
        if (patternProbability != 100) return false;
        if (humanizeRange != 0) return false;
        if (seedSet) return false;
        if (coupling != CouplingMode::INDEPENDENT) return false;
        for (uint8_t i = 0; i < kMaxSteps; ++i) {
            if (stepProbability[i] != STEP_PROB_INHERIT) return false;
        }
        return true;
    }
};

}  // namespace common
