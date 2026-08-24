#include "GateSetView.h"
#include "../state/Reducer.h"
#include "../Types.h"
#include <cstdio>

namespace ui {

GateSetView::GateSetView(hardware::LedMatrix& ledMatrix)
    : ledMatrix(ledMatrix) {}

void GateSetView::onEnter()
{
    printf("Entering Gate Set View\n");
    ledMatrix.clear();
}

state::UIState GateSetView::handleEvent(const state::UIState& state, const events::Event& event)
{
    state::UIState newState = state;
    if (event.type == events::EventType::KEY_PRESSED &&
        combo(event.data.key.id, event.data.key.mods) == combo(KeyId::ESCAPE)) {
        state::setCurrentView(newState, state::ViewId::PATTERNS);
    }
    return newState;
}

void GateSetView::render(const state::UIState&)
{
    ledMatrix.clear();
    ledMatrix.drawLabel("GAt", 0xFFFFEE00);
}

}
