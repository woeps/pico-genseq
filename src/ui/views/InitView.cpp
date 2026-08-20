#include "InitView.h"
#include "../../commands/command.h"
#include "../state/Reducer.h"
#include <cstdio>

namespace ui {

InitView::InitView(hardware::Led& led, hardware::LedMatrix& ledMatrix) :
    led(led),
    ledMatrix(ledMatrix)
{}

void InitView::onEnter()
{
    printf("Entering Main View\n");
    ledMatrix.clear();
}

state::UIState InitView::handleEvent(const state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    state::UIState newState = state;

    switch (combo(event.data.key.id, event.data.key.mods)) {
        case combo(KeyId::UP):               state::setValue(newState, state.value + 1);  break;
        case combo(KeyId::UP,   mod::SHIFT): state::setValue(newState, state.value + 10); break;
        case combo(KeyId::DOWN):             state::setValue(newState, state.value - 1);  break;
        case combo(KeyId::DOWN, mod::SHIFT): state::setValue(newState, state.value - 10); break;
        default: break;
    }

    return newState;
}

void InitView::render(const state::UIState& state)
{
    // Update LED based on playing state
    if (state.playing) {
        led.blink(500, 500);
    } else {
        led.off();
    }

    ledMatrix.drawNumber(state.value, 0xFFFF0011);
    ledMatrix.drawLabel("tst", 0x0000FF33);

}

} // namespace ui
