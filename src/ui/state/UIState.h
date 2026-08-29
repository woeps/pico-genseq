#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "../../common/pitch_set.h"

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
};

enum class NoteLength : uint8_t {
    THIRTY_SECOND,
    SIXTEENTH,
    EIGHTH,
    QUARTER,
    HALF,
    WHOLE,
};

struct GateSetConfig {
    GateAlgorithm algorithm = GateAlgorithm::EUCLIDEAN;
    uint8_t steps = 16;
    uint8_t pulses = 4;
    uint8_t rotation = 0;
    NoteLength noteLength = NoteLength::SIXTEENTH;
    uint8_t gateLength = 50;
};

inline bool operator==(const GateSetConfig& lhs, const GateSetConfig& rhs) {
    return lhs.algorithm == rhs.algorithm && lhs.steps == rhs.steps &&
           lhs.pulses == rhs.pulses && lhs.rotation == rhs.rotation &&
           lhs.noteLength == rhs.noteLength && lhs.gateLength == rhs.gateLength;
}

inline bool operator!=(const GateSetConfig& lhs, const GateSetConfig& rhs) {
    return !(lhs == rhs);
}

constexpr uint8_t MAX_PITCHES = 16;

// PitchSetConfig field indices: 0 = COUNT, 1 = ORDER, 2..2+count-1 = pitches
constexpr uint8_t PITCH_SET_FIELD_COUNT = 0;
constexpr uint8_t PITCH_SET_FIELD_ORDER = 1;
constexpr uint8_t PITCH_SET_FIELD_PITCH_BASE = 2;

struct PitchSetConfig {
    uint8_t count = 4;
    common::PlayingOrder order = common::PlayingOrder::FORWARDS;
    std::array<uint8_t, MAX_PITCHES> pitches{60, 67, 69, 72, 0, 0, 0, 0,
                                              0, 0, 0, 0, 0, 0, 0, 0};
};

inline bool operator==(const PitchSetConfig& lhs, const PitchSetConfig& rhs) {
    if (lhs.count != rhs.count || lhs.order != rhs.order) return false;
    for (uint8_t i = 0; i < lhs.count; i++) {
        if (lhs.pitches[i] != rhs.pitches[i]) return false;
    }
    return true;
}

inline bool operator!=(const PitchSetConfig& lhs, const PitchSetConfig& rhs) {
    return !(lhs == rhs);
}

constexpr uint8_t MAX_VELOCITIES = 16;

// VelocitySetConfig field indices: 0 = COUNT, 1 = ORDER, 2..count+1 = velocities
constexpr uint8_t VELOCITY_SET_FIELD_COUNT = 0;
constexpr uint8_t VELOCITY_SET_FIELD_ORDER = 1;
constexpr uint8_t VELOCITY_SET_FIELD_VELOCITY_BASE = 2;

struct VelocitySetConfig {
    uint8_t count = 4;
    common::PlayingOrder order = common::PlayingOrder::FORWARDS;
    std::array<uint8_t, MAX_VELOCITIES> velocities{100, 80, 60, 80, 0, 0, 0, 0,
                                                     0,   0,  0,  0, 0, 0, 0, 0};
};

inline bool operator==(const VelocitySetConfig& lhs, const VelocitySetConfig& rhs) {
    if (lhs.count != rhs.count || lhs.order != rhs.order) return false;
    for (uint8_t i = 0; i < lhs.count; i++) {
        if (lhs.velocities[i] != rhs.velocities[i]) return false;
    }
    return true;
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
};

} // namespace ui::state
