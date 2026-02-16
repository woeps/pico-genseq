#pragma once

#include "IView.h"
#include "../hardware/Led.h"
#include "../hardware/LedMatrix.h"

namespace ui {

class SettingsView : public IView {
public:
    SettingsView(hardware::Led& led, hardware::LedMatrix& ledMatrix);

    void onEnter() override;
    void render(const state::UIState& state) override;

private:
    hardware::Led& led;
    hardware::LedMatrix& ledMatrix;
};

} // namespace ui
