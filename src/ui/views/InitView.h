#pragma once

#include "IView.h"
#include "../hardware/Led.h"
#include "../hardware/LedMatrix.h"

namespace ui {

class InitView : public IView {
public:
    InitView(hardware::Led& led, hardware::LedMatrix& ledMatrix);

    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state, const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    hardware::Led& led;
    hardware::LedMatrix& ledMatrix;
};

} // namespace ui
