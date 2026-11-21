#pragma once

#include "IView.h"
#include "../hardware/LedMatrix.h"

namespace ui {

// Instruments view (F2): channel, mute, and which patterns each Instrument plays.
class InstrumentsView : public IView {
public:
    InstrumentsView(hardware::LedMatrix& ledMatrix);

    void onEnter() override;
    void handleEvent(state::UIState& state, const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    hardware::LedMatrix& ledMatrix;
};

} // namespace ui
