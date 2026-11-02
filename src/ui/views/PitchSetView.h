#pragma once

#include "IView.h"
#include "../hardware/LedMatrix.h"

namespace ui {

class PitchSetView : public IView {
public:
    explicit PitchSetView(hardware::LedMatrix& ledMatrix);

    void onEnter() override;
    void handleEvent(state::UIState& state, const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    hardware::LedMatrix& ledMatrix;
};

}
