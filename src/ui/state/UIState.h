#pragma once

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

constexpr size_t VIEW_COUNT = static_cast<size_t>(ViewId::COUNT);
constexpr uint8_t MAX_PATTERNS = 15;
static_assert(MAX_PATTERNS <= 16);

struct UIState {
    ViewId currentView;
    uint8_t bpm;
    bool playing;
    int value;
    uint8_t patternCount;
    uint16_t activePatterns;
    uint8_t selectedPattern;
    PatternSet selectedPatternSet;

    UIState() : currentView(ViewId::INIT), bpm(120), playing(false), value(0),
                patternCount(1), activePatterns(0x0001), selectedPattern(0),
                selectedPatternSet(PatternSet::GATE) {}
};

} // namespace ui::state
