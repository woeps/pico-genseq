#include "SettingsView.h"
#include "../state/Reducer.h"
#include <cstdio>

namespace ui {

SettingsView::SettingsView(hardware::Led& led, hardware::LedMatrix& ledMatrix)
    : led(led), ledMatrix(ledMatrix) {}

void SettingsView::onEnter()
{
    printf("Entering Settings View\n");
    ledMatrix.clear();
}

state::UIState SettingsView::handleEvent(const state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    state::UIState newState = state;

    switch (combo(event.data.key.id, event.data.key.mods)) {
        case combo(KeyId::UP):               state::setBpm(newState, state.bpm + 1);  break;
        case combo(KeyId::UP,   mod::SHIFT): state::setBpm(newState, state.bpm + 10); break;
        case combo(KeyId::DOWN):             state::setBpm(newState, state.bpm - 1);  break;
        case combo(KeyId::DOWN, mod::SHIFT): state::setBpm(newState, state.bpm - 10); break;
        default: break;
    }

    return newState;
}

void SettingsView::render(const state::UIState& state)
{
    ledMatrix.drawLabel("bPM", 0xFF00FF33);
    ledMatrix.drawNumber(state.bpm, 0xFFFF0011);
}

} // namespace ui
