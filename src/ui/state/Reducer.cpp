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
    state.value = value;
}

void setCurrentView(UIState& state, ViewId viewId) {
    state.currentView = viewId;
}

UIState reduce(const UIState& state, const events::Event& event, ui::IView* activeView) {
    if (activeView) {
        return activeView->handleEvent(state, event);
    }
    return state;
}

} // namespace ui::state
