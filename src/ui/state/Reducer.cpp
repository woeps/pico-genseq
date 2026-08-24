#include "Reducer.h"
#include "../../commands/command.h"
#include "../../common/gate_set.h"
#include <algorithm>
#include <cstdio>

namespace ui::state {

namespace {

constexpr GateSetProperty EUCLIDEAN_PROPERTIES[] = {
    GateSetProperty::ALGORITHM,
    GateSetProperty::STEPS,
    GateSetProperty::PULSES,
    GateSetProperty::ROTATION,
    GateSetProperty::NOTE_LENGTH,
    GateSetProperty::LENGTH,
};

uint8_t noteLengthTicks(NoteLength noteLength) {
    switch (noteLength) {
        case NoteLength::THIRTY_SECOND: return 3;
        case NoteLength::SIXTEENTH: return 6;
        case NoteLength::EIGHTH: return 12;
        case NoteLength::QUARTER: return 24;
        case NoteLength::HALF: return 48;
        case NoteLength::WHOLE: return 96;
    }
    return 6;
}

void sendGateSet(uint8_t patternIndex, const GateSetConfig& config) {
    switch (config.algorithm) {
        case GateAlgorithm::EUCLIDEAN: {
            const common::GateSet gateSet = common::GateSet::createEuclidean(
                config.steps, config.pulses, config.rotation,
                noteLengthTicks(config.noteLength), config.gateLength);
            commands::sendGateSet(patternIndex, gateSet.getGates());
            break;
        }
    }
}

void clampEuclidean(GateSetConfig& config) {
    config.gateLength = std::min<uint8_t>(config.gateLength, 100);
    config.steps = std::clamp<uint8_t>(config.steps, 1, 64);
    config.pulses = std::min(config.pulses, config.steps);
    config.rotation = std::min(config.rotation, static_cast<uint8_t>(config.steps - 1));
}

void restoreGateSetDraft(UIState& state) {
    if (!state.gateSetDirty || state.selectedPattern >= state.patternCount) return;
    state.gateSetDraft = state.gateSetConfigs[state.selectedPattern];
    state.gateSetDirty = false;
    sendGateSet(state.selectedPattern, state.gateSetDraft);
}

}

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
    if (state.currentView == ViewId::GATE_SET && viewId != ViewId::GATE_SET) {
        restoreGateSetDraft(state);
    }
    state.currentView = viewId;
}

void beginGateSetEdit(UIState& state) {
    if (state.selectedPattern >= state.patternCount) return;
    state.gateSetDraft = state.gateSetConfigs[state.selectedPattern];
    state.selectedGateSetProperty = GateSetProperty::ALGORITHM;
    state.gateSetDirty = false;
}

void moveGateSetProperty(UIState& state, int direction) {
    const GateSetProperty* properties = nullptr;
    size_t propertyCount = 0;
    switch (state.gateSetDraft.algorithm) {
        case GateAlgorithm::EUCLIDEAN:
            properties = EUCLIDEAN_PROPERTIES;
            propertyCount = sizeof(EUCLIDEAN_PROPERTIES) / sizeof(EUCLIDEAN_PROPERTIES[0]);
            break;
    }

    size_t index = 0;
    while (index < propertyCount && properties[index] != state.selectedGateSetProperty) index++;
    if (index == propertyCount) index = 0;
    const int next = std::clamp(static_cast<int>(index) + direction, 0,
                                static_cast<int>(propertyCount) - 1);
    state.selectedGateSetProperty = properties[next];
}

void adjustGateSetValue(UIState& state, int delta) {
    if (state.selectedPattern >= state.patternCount || delta == 0) return;
    GateSetConfig next = state.gateSetDraft;

    switch (next.algorithm) {
        case GateAlgorithm::EUCLIDEAN:
            switch (state.selectedGateSetProperty) {
                case GateSetProperty::ALGORITHM: return;
                case GateSetProperty::STEPS:
                    next.steps = std::clamp(static_cast<int>(next.steps) + delta, 1, 64);
                    break;
                case GateSetProperty::PULSES:
                    next.pulses = std::clamp(static_cast<int>(next.pulses) + delta, 0,
                                             static_cast<int>(next.steps));
                    break;
                case GateSetProperty::ROTATION:
                    next.rotation = std::clamp(static_cast<int>(next.rotation) + delta, 0,
                                               static_cast<int>(next.steps) - 1);
                    break;
                case GateSetProperty::NOTE_LENGTH:
                    next.noteLength = static_cast<NoteLength>(std::clamp(
                        static_cast<int>(next.noteLength) + delta,
                        static_cast<int>(NoteLength::THIRTY_SECOND),
                        static_cast<int>(NoteLength::WHOLE)));
                    break;
                case GateSetProperty::LENGTH:
                    next.gateLength = std::clamp(static_cast<int>(next.gateLength) + delta, 0, 100);
                    break;
            }
            clampEuclidean(next);
            break;
    }

    if (next == state.gateSetDraft) return;
    state.gateSetDraft = next;
    state.gateSetDirty = next != state.gateSetConfigs[state.selectedPattern];
    sendGateSet(state.selectedPattern, next);
}

void commitGateSetEdit(UIState& state) {
    if (state.selectedPattern >= state.patternCount) return;
    state.gateSetConfigs[state.selectedPattern] = state.gateSetDraft;
    state.gateSetDirty = false;
}

void undoGateSetEdit(UIState& state) {
    restoreGateSetDraft(state);
}

void syncGateSet(const UIState& state, uint8_t index) {
    if (index < state.patternCount) sendGateSet(index, state.gateSetConfigs[index]);
}

void setSelectedPattern(UIState& state, uint8_t index) {
    state.selectedPattern = std::min(index, static_cast<uint8_t>(state.patternCount));
}

void setSelectedPatternSet(UIState& state, PatternSet patternSet) {
    state.selectedPatternSet = patternSet;
}

void addPattern(UIState& state) {
    if (state.patternCount >= MAX_PATTERNS) return;
    const uint8_t newPattern = state.patternCount;
    state.gateSetConfigs[newPattern] = GateSetConfig{};
    state.patternCount++;
    state.activePatterns |= (1 << newPattern);
    state.selectedPattern = newPattern;
    commands::sendCommand(commands::Command::PATTERN_ADD);
    syncGateSet(state, newPattern);
}

void removePattern(UIState& state, uint8_t index) {
    if (state.patternCount <= 1 || index >= state.patternCount) return;

    const uint16_t lowerMask = index == 0 ? 0 : static_cast<uint16_t>((1u << index) - 1);
    const uint16_t lower = state.activePatterns & lowerMask;
    const uint16_t upper = state.activePatterns >> (index + 1);
    state.activePatterns = lower | static_cast<uint16_t>(upper << index);
    for (uint8_t i = index; i + 1 < state.patternCount; i++) {
        state.gateSetConfigs[i] = state.gateSetConfigs[i + 1];
    }
    state.gateSetConfigs[state.patternCount - 1] = GateSetConfig{};
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
