#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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
};

} // namespace ui::state
