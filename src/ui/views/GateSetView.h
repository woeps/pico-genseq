#pragma once

#include "IView.h"
#include "../hardware/LedMatrix.h"

namespace ui {

class GateSetView : public IView {
public:
    explicit GateSetView(hardware::LedMatrix& ledMatrix);

    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state, const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    hardware::LedMatrix& ledMatrix;
};

}
