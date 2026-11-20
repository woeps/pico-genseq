#pragma once

#include "IView.h"
#include "../hardware/LedMatrix.h"

namespace ui {

class VelocitySetView : public IView {
public:
    explicit VelocitySetView(hardware::LedMatrix& ledMatrix);

    void onEnter() override;
    void handleEvent(state::UIState& state, const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    hardware::LedMatrix& ledMatrix;
};

}
