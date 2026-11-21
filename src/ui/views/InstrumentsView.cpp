#include "InstrumentsView.h"
#include "../state/Reducer.h"

namespace ui {

InstrumentsView::InstrumentsView(hardware::LedMatrix& ledMatrix) : ledMatrix(ledMatrix) {}

void InstrumentsView::onEnter()
{
    ledMatrix.clear();
}

void InstrumentsView::handleEvent(state::UIState&, const events::Event&)
{
}

void InstrumentsView::render(const state::UIState&)
{
    ledMatrix.drawLabel("INS", 0xFFFFEE00);
}

} // namespace ui
