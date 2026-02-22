#include "SettingsView.h"
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
    // TODO: Implement settings event handling
    return state;
}

void SettingsView::render(const state::UIState& state)
{
    // TODO: Implement settings view rendering
    ledMatrix.drawLabel("SET", 0xFF00FF33);
}

} // namespace ui
