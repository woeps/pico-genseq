#include "Reducer.h"
#include "../../commands/command.h"
#include <algorithm>
#include <cstdio>

namespace ui::state {

void setBpm(UIState& state, int bpm) {
    state.bpm = std::max(40, std::min(255, bpm));
    commands::sendCommand(commands::Command::BPM_SET, state.bpm);
}

void setPlaying(UIState& state, bool playing) {
    state.playing = playing;
    if (playing) {
        commands::sendCommand(commands::Command::PLAY);
    } else {
        commands::sendCommand(commands::Command::STOP);
    }
}

void setValue(UIState& state, int value) {
    state.value = std::max(0, std::min(99, value));
}

void setCurrentView(UIState& state, ViewId viewId) {
    state.currentView = viewId;
}

void setSelectedPattern(UIState& state, uint8_t index) {
    state.selectedPattern = std::min(index, static_cast<uint8_t>(state.patternCount));
}

void setSelectedPatternSet(UIState& state, PatternSet patternSet) {
    state.selectedPatternSet = patternSet;
}

void addPattern(UIState& state) {
    if (state.patternCount >= MAX_PATTERNS) return;
    state.patternCount++;
    state.activePatterns |= (1 << (state.patternCount - 1));
    state.selectedPattern = state.patternCount - 1;
    commands::sendCommand(commands::Command::PATTERN_ADD);
}

void removePattern(UIState& state, uint8_t index) {
    if (state.patternCount <= 1 || index >= state.patternCount) return;

    const uint16_t lowerMask = index == 0 ? 0 : static_cast<uint16_t>((1u << index) - 1);
    const uint16_t lower = state.activePatterns & lowerMask;
    const uint16_t upper = state.activePatterns >> (index + 1);
    state.activePatterns = lower | static_cast<uint16_t>(upper << index);
    state.patternCount--;
    if (state.selectedPattern > index) state.selectedPattern--;
    state.selectedPattern = std::min(state.selectedPattern,
                                     static_cast<uint8_t>(state.patternCount - 1));
    commands::sendCommand(commands::Command::PATTERN_REMOVE, index);
}

void togglePatternActive(UIState& state, uint8_t index) {
    if (index >= state.patternCount) return;
    state.activePatterns ^= (1 << index);
    if (state.activePatterns & (1 << index)) {
        commands::sendCommand(commands::Command::PATTERN_ACTIVATE, index);
    } else {
        commands::sendCommand(commands::Command::PATTERN_DEACTIVATE, index);
    }
}

namespace {

constexpr uint8_t F1_USAGE  = static_cast<uint8_t>(KeyId::F1);
constexpr uint8_t F12_USAGE = static_cast<uint8_t>(KeyId::F12);

// F1 -> INIT, F2 -> SETTINGS. Keys past the end of this table are reserved
// for future views: consumed, traced, but without effect.
constexpr ViewId FUNCTION_KEY_VIEWS[] = { ViewId::INIT, ViewId::SETTINGS, ViewId::PATTERNS };

bool isFunctionKey(KeyId id) {
    const uint8_t usage = static_cast<uint8_t>(id);
    return usage >= F1_USAGE && usage <= F12_USAGE;
}

// Reserved by key, independent of modifiers, so shift+F1 is swallowed too
// and modified function keys stay available for future global bindings.
bool isReserved(KeyId id) {
    return id == KeyId::SPACE || isFunctionKey(id);
}

void applyGlobal(UIState& state, KeyId id) {
    if (id == KeyId::SPACE) {
        setPlaying(state, !state.playing);
        return;
    }

    const size_t index = static_cast<size_t>(static_cast<uint8_t>(id) - F1_USAGE);
    if (index < (sizeof(FUNCTION_KEY_VIEWS) / sizeof(FUNCTION_KEY_VIEWS[0]))) {
        setCurrentView(state, FUNCTION_KEY_VIEWS[index]);
    } else {
        printf("F%u pressed - no view bound\n", static_cast<unsigned>(index + 1));
    }
}

} // namespace

UIState reduce(const UIState& state, const events::Event& event, ui::IView* activeView) {
    if (isReserved(event.data.key.id)) {
        UIState next = state;
        if (event.type == events::EventType::KEY_PRESSED &&
            event.data.key.mods == mod::NONE) {
            applyGlobal(next, event.data.key.id);
        }
        return next;    // consumed either way - never reaches a view
    }

    if (activeView) {
        return activeView->handleEvent(state, event);
    }
    return state;
}

} // namespace ui::state
