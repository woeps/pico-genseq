#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "../../common/pitch_set.h"
#include "../../common/GenerationParameters.h"

namespace ui::state {

enum class ViewId : uint8_t {
    INIT,
    SETTINGS,
    PATTERNS,
    GATE_SET,
    PITCH_SET,
    VELOCITY_SET,
    COUNT       // sentinel - keep last
};

enum class PatternSet : uint8_t {
    GATE,
    PITCH,
    VELOCITY,
};

// Transient save-feedback banner state (persist-settings feature).
// Drives the LCD feedback presented by the core0 UI loop; NONE means no
// banner is pending. NOT_LOADED is the boot-time fallback shown when the
// saved config could not be loaded (Req 6.7).
enum class SaveBanner : uint8_t {
    NONE,
    SUCCESS,
    FAILURE,
    NOT_LOADED,
};

enum class GateAlgorithm : uint8_t {
    EUCLIDEAN,
};

enum class GateSetProperty : uint8_t {
    ALGORITHM,
    STEPS,
    PULSES,
    ROTATION,
    NOTE_LENGTH,
    LENGTH,
    PROBABILITY,  // R2/R10: per-Pattern Step Probability (0..100)
    STEP_GRID,  // generative-engine-expansion R9: per-step gate + probability grid
};

enum class NoteLength : uint8_t {
    THIRTY_SECOND,
    SIXTEENTH,
    EIGHTH,
    QUARTER,
    HALF,
    WHOLE,
};

// R9 per-step grid limits. The grid addresses PROGRAMMED steps (0..steps-1),
// capped at the per-step probability array size the engine carries
// (common::GenerationParameters::kMaxSteps == 16). Manual gate editing toggles a
// whole programmed step on/off.
constexpr uint8_t MAX_GRID_STEPS = 16;
// Per-step probability sentinel mirrors common::STEP_PROB_INHERIT.
constexpr uint8_t STEP_PROB_INHERIT = 0xFF;

struct GateSetConfig {
    GateAlgorithm algorithm = GateAlgorithm::EUCLIDEAN;
    uint8_t steps = 16;
    uint8_t pulses = 4;
    uint8_t rotation = 0;
    NoteLength noteLength = NoteLength::SIXTEENTH;
    uint8_t gateLength = 50;

    // R9: explicit per-programmed-step gate states and per-step probability. The
    // generator writes gateStates (the "optional seed"); manual edits layer on
    // top. probability is INHERIT by default (falls back to per-Pattern), so an
    // untouched config is behavior-preserving. Only [0, steps) are meaningful.
    uint8_t gateStates[MAX_GRID_STEPS] = {0};
    uint8_t stepProbability[MAX_GRID_STEPS] = {
        STEP_PROB_INHERIT, STEP_PROB_INHERIT, STEP_PROB_INHERIT, STEP_PROB_INHERIT,
        STEP_PROB_INHERIT, STEP_PROB_INHERIT, STEP_PROB_INHERIT, STEP_PROB_INHERIT,
        STEP_PROB_INHERIT, STEP_PROB_INHERIT, STEP_PROB_INHERIT, STEP_PROB_INHERIT,
        STEP_PROB_INHERIT, STEP_PROB_INHERIT, STEP_PROB_INHERIT, STEP_PROB_INHERIT};
    // Per-Pattern Step Probability (R2), 0..100. 100 == today's behavior.
    uint8_t patternProbability = 100;
};

inline bool operator==(const GateSetConfig& lhs, const GateSetConfig& rhs) {
    if (!(lhs.algorithm == rhs.algorithm && lhs.steps == rhs.steps &&
          lhs.pulses == rhs.pulses && lhs.rotation == rhs.rotation &&
          lhs.noteLength == rhs.noteLength && lhs.gateLength == rhs.gateLength &&
          lhs.patternProbability == rhs.patternProbability)) {
        return false;
    }
    const uint8_t n = lhs.steps < MAX_GRID_STEPS ? lhs.steps : MAX_GRID_STEPS;
    for (uint8_t i = 0; i < n; ++i) {
        if (lhs.gateStates[i] != rhs.gateStates[i]) return false;
        if (lhs.stepProbability[i] != rhs.stepProbability[i]) return false;
    }
    return true;
}

inline bool operator!=(const GateSetConfig& lhs, const GateSetConfig& rhs) {
    return !(lhs == rhs);
}

constexpr uint8_t MAX_PITCHES = 16;

// PitchSetConfig field indices. The scale controls (generative-engine-expansion
// R8/R10) come FIRST so the scale context sits with the membership cue, ahead of
// the pitches it constrains:
//   0 = SCALE, 1 = ROOT, 2 = QUANTIZE, 3 = COUNT, 4 = ORDER,
//   5.. = pitches[0..count-1]
constexpr uint8_t PITCH_SET_FIELD_SCALE = 0;
constexpr uint8_t PITCH_SET_FIELD_ROOT = 1;
constexpr uint8_t PITCH_SET_FIELD_QUANTIZE = 2;
constexpr uint8_t PITCH_SET_FIELD_COUNT = 3;
constexpr uint8_t PITCH_SET_FIELD_ORDER = 4;
constexpr uint8_t PITCH_SET_FIELD_PITCH_BASE = 5;

struct PitchSetConfig {
    uint8_t count = 4;
    common::PlayingOrder order = common::PlayingOrder::FORWARDS;
    std::array<uint8_t, MAX_PITCHES> pitches{60, 67, 69, 72, 0, 0, 0, 0,
                                              0, 0, 0, 0, 0, 0, 0, 0};
    // Scale constraint (R1/R8), behavior-preserving defaults: CHROMATIC == no
    // constraint, so an untouched PitchSet renders and sounds exactly as before.
    common::ScaleId scale = common::ScaleId::CHROMATIC;
    uint8_t scaleRoot = 0;  // 0..11 (0 == C)
    common::QuantizeMode quantize = common::QuantizeMode::NEAREST;
};

inline bool operator==(const PitchSetConfig& lhs, const PitchSetConfig& rhs) {
    if (lhs.count != rhs.count || lhs.order != rhs.order) return false;
    for (uint8_t i = 0; i < lhs.count; i++) {
        if (lhs.pitches[i] != rhs.pitches[i]) return false;
    }
    return lhs.scale == rhs.scale && lhs.scaleRoot == rhs.scaleRoot &&
           lhs.quantize == rhs.quantize;
}

inline bool operator!=(const PitchSetConfig& lhs, const PitchSetConfig& rhs) {
    return !(lhs == rhs);
}

constexpr uint8_t MAX_VELOCITIES = 16;

// VelocitySetConfig field indices. HUMANIZE/SEED/COUPLING (the remaining
// Pattern-scope Generation Parameters, R10) come FIRST, then COUNT/ORDER/
// velocities. Seed and coupling are per-Pattern (not velocity-specific) but live
// here because the VelocitySet view is the per-pattern editor with the move/
// adjust vocabulary R10 requires; HUMANIZE is naturally a velocity concept.
constexpr uint8_t VELOCITY_SET_FIELD_HUMANIZE = 0;
constexpr uint8_t VELOCITY_SET_FIELD_SEED = 1;
constexpr uint8_t VELOCITY_SET_FIELD_COUPLING = 2;
constexpr uint8_t VELOCITY_SET_FIELD_COUNT = 3;
constexpr uint8_t VELOCITY_SET_FIELD_ORDER = 4;
constexpr uint8_t VELOCITY_SET_FIELD_VELOCITY_BASE = 5;

struct VelocitySetConfig {
    uint8_t count = 4;
    common::PlayingOrder order = common::PlayingOrder::FORWARDS;
    std::array<uint8_t, MAX_VELOCITIES> velocities{100, 80, 60, 80, 0, 0, 0, 0,
                                                     0,   0,  0,  0, 0, 0, 0, 0};
    // Pattern-scope Generation Parameters (R3/R4/R5), behavior-preserving
    // defaults: humanize 0, no seed, independent coupling.
    uint8_t humanizeRange = 0;                                 // +/- amount
    bool seedSet = false;
    uint32_t seed = 0;
    common::CouplingMode coupling = common::CouplingMode::INDEPENDENT;
};

inline bool operator==(const VelocitySetConfig& lhs, const VelocitySetConfig& rhs) {
    if (lhs.count != rhs.count || lhs.order != rhs.order) return false;
    for (uint8_t i = 0; i < lhs.count; i++) {
        if (lhs.velocities[i] != rhs.velocities[i]) return false;
    }
    return lhs.humanizeRange == rhs.humanizeRange && lhs.seedSet == rhs.seedSet &&
           lhs.seed == rhs.seed && lhs.coupling == rhs.coupling;
}

inline bool operator!=(const VelocitySetConfig& lhs, const VelocitySetConfig& rhs) {
    return !(lhs == rhs);
}

constexpr size_t VIEW_COUNT = static_cast<size_t>(ViewId::COUNT);
constexpr uint8_t MAX_PATTERNS = 15;
static_assert(MAX_PATTERNS <= 16);

struct UIState {
    ViewId currentView = ViewId::INIT;
    uint8_t bpm = 120;
    bool playing = false;
    int value = 0;
    uint8_t patternCount = 1;
    uint16_t activePatterns = 0x0001;
    uint8_t selectedPattern = 0;
    PatternSet selectedPatternSet = PatternSet::GATE;
    std::array<GateSetConfig, MAX_PATTERNS> gateSetConfigs{};
    GateSetConfig gateSetDraft{};
    GateSetProperty selectedGateSetProperty = GateSetProperty::ALGORITHM;
    // R9 grid: which programmed step (0..steps-1) is selected while the
    // STEP_GRID property is active.
    uint8_t selectedGateStep = 0;
    bool gateSetDirty = false;
    std::array<PitchSetConfig, MAX_PATTERNS> pitchSetConfigs{};
    PitchSetConfig pitchSetDraft{};
    uint8_t selectedPitchSetField = PITCH_SET_FIELD_COUNT;
    bool pitchSetDirty = false;
    std::array<VelocitySetConfig, MAX_PATTERNS> velocitySetConfigs{};
    VelocitySetConfig velocitySetDraft{};
    uint8_t selectedVelocitySetField = VELOCITY_SET_FIELD_COUNT;
    bool velocitySetDirty = false;

    // --- Persist-settings save status (transient, plain state) ---
    // Set by the pure reducer when Ctrl+S is recognized (Req 1.4), drained by
    // the core0 UI loop which performs the actual flash save. The reducer never
    // touches flash.
    bool saveRequested = false;
    // Transient banner driving the LCD feedback (Req 7.2 / 6.7). NOT_LOADED is
    // set on boot when the saved config could not be loaded, so a separate
    // boot flag is not required (Req 6.7).
    SaveBanner saveBanner = SaveBanner::NONE;

    // --- Timing Fault indication (performance-safe-foundation task 11.1) ---
    // Latched fail-closed indicator (Req 10.3, 10.4, 10.7). DISTINCT from the
    // SaveBanner states (success/failure/not-loaded) so the matrix can present
    // it unambiguously (Req 10.4). It latches when core1 publishes a Timing
    // Fault and is cleared only by an explicit Play acknowledgement (Req 10.6);
    // it NEVER auto-dismisses (Req 10.5). Working State (patterns + their
    // configuration above) is PRESERVED while this is latched (Req 10.3) — the
    // fault flag is independent plain state and does not clear any config.
    bool timingFaultLatched = false;
    // Machine-readable fault cause for diagnostics (Req 10.7); mirrors
    // sequencer::PerformanceFaultCause as a plain byte so UIState stays free of
    // a sequencer include. 0 == NONE.
    uint8_t timingFaultCause = 0;
};

} // namespace ui::state
