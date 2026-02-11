#pragma once

#include "../views/IView.h"
#include "../hardware/interfaces/IOutput.h"
#include <cstdint>

namespace ui {

// Main view for the sequencer UI
class MainView : public IView {
public:
    MainView(hardware::IOutput& display, hardware::IOutput& led);

    void onEnter() override;
    void onExit() override;
    void update() override;
    void onEncoderChanged(int delta) override;
    void onButtonPressed(ButtonId button) override;
    void onButtonHeld(ButtonId button) override;
    void render() override;

private:
    hardware::IOutput& display;
    hardware::IOutput& led;

    // View state
    uint8_t bpm;
    bool playing;
    uint32_t lastUpdateTime;
};

} // namespace ui
