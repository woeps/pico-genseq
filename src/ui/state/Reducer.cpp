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

UIState reduce(const UIState& state, const events::Event& event) {
    UIState newState = state;

    switch(state.currentView) {
        case ViewId::INIT:
            switch (event.type) {
                case events::EventType::BUTTON_PRESSED:
                    if (event.data.button.id == ButtonId::BUTTON_A) {
                        setValue(newState, 0);
                    }
                    // Example: Switch to SETTINGS view on BUTTON_B press
                    else if (event.data.button.id == ButtonId::BUTTON_B) {
                        // setCurrentView(newState, ViewId::SETTINGS);
                        // printf("Switched to Settings view\n");
                        setValue(newState, 1);
                    }
                    else if (event.data.button.id == ButtonId::BUTTON_C) {
                        setValue(newState, 2);
                    }
                    else if (event.data.button.id == ButtonId::BUTTON_D) {
                        setValue(newState, 3);
                    }
                    else if (event.data.button.id == ButtonId::BUTTON_E) {
                        setValue(newState, 4);
                    }
                    else if (event.data.button.id == ButtonId::BUTTON_F) {
                        setValue(newState, 5);
                    }
                    break;

                case events::EventType::BUTTON_HELD:
                    if (event.data.button.id == ButtonId::BUTTON_F) {
                        setCurrentView(newState, ViewId::SETTINGS);
                        printf("Switched to Settings view\n");
                    }
                    break;

                case events::EventType::POT_CHANGED: {
                    int v = (event.data.pot.value * 99) / 4095;
                    // printf("POT dispatch: value=%d\n", v);
                    setValue(newState, v);
                    break;
                }


                default:
                    break;
            }
            break;
        case ViewId::SETTINGS:
            break;
    }

    return newState;
}

} // namespace ui::state
