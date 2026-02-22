#include "InitView.h"
#include "../../commands/command.h"
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
    state::UIState newState = state;

    switch (event.type) {
        case events::EventType::BUTTON_PRESSED:
            if (event.data.button.id == ButtonId::BUTTON_A) {
                newState.value = 0;
            } else if (event.data.button.id == ButtonId::BUTTON_B) {
                newState.value = 1;
            } else if (event.data.button.id == ButtonId::BUTTON_C) {
                newState.value = 2;
            } else if (event.data.button.id == ButtonId::BUTTON_D) {
                newState.value = 3;
            } else if (event.data.button.id == ButtonId::BUTTON_E) {
                newState.value = 4;
            } else if (event.data.button.id == ButtonId::BUTTON_F) {
                newState.value = 5;
            }
            break;

        case events::EventType::BUTTON_HELD:
            if (event.data.button.id == ButtonId::BUTTON_F) {
                state::setCurrentView(newState, state::ViewId::SETTINGS);
                printf("Switched to Settings view\n");
            }
            break;

        case events::EventType::POT_CHANGED: {
            int v = (event.data.pot.value * 99) / 4095;
            newState.value = v;
            break;
        }

        default:
            break;
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
