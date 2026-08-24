#include "PitchSetView.h"
#include "../state/Reducer.h"
#include "../Types.h"
#include <cstdio>

namespace ui {

PitchSetView::PitchSetView(hardware::LedMatrix& ledMatrix)
    : ledMatrix(ledMatrix) {}

void PitchSetView::onEnter()
{
    printf("Entering Pitch Set View\n");
    ledMatrix.clear();
}

state::UIState PitchSetView::handleEvent(const state::UIState& state, const events::Event& event)
{
    state::UIState newState = state;
    if (event.type == events::EventType::KEY_PRESSED &&
        combo(event.data.key.id, event.data.key.mods) == combo(KeyId::ESCAPE)) {
        state::setCurrentView(newState, state::ViewId::PATTERNS);
    }
    return newState;
}

void PitchSetView::render(const state::UIState&)
{
    ledMatrix.clear();
    ledMatrix.drawLabel("PIt", 0xFFFFEE00);
}

}
