#include "VelocitySetView.h"
#include "../state/Reducer.h"
#include "../Types.h"
#include <cstdio>

namespace ui {

VelocitySetView::VelocitySetView(hardware::LedMatrix& ledMatrix)
    : ledMatrix(ledMatrix) {}

void VelocitySetView::onEnter()
{
    printf("Entering Velocity Set View\n");
    ledMatrix.clear();
}

state::UIState VelocitySetView::handleEvent(const state::UIState& state, const events::Event& event)
{
    state::UIState newState = state;
    if (event.type == events::EventType::KEY_PRESSED &&
        combo(event.data.key.id, event.data.key.mods) == combo(KeyId::ESCAPE)) {
        state::setCurrentView(newState, state::ViewId::PATTERNS);
    }
    return newState;
}

void VelocitySetView::render(const state::UIState&)
{
    ledMatrix.clear();
    ledMatrix.drawLabel("VEL", 0xFFFFEE00);
}

}
