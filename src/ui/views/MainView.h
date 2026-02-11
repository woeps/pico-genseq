#pragma once

#include "../views/IView.h"
#include "../hardware/interfaces/IActuator.h"
#include <cstdint>

namespace ui {

// Main view for the sequencer UI
class MainView : public IView {
public:
    MainView(hardware::IActuator& display, hardware::IActuator& led);

    void onEnter() override;
    void onExit() override;
    void update() override;
    void onEncoderChanged(int delta) override;
    void onButtonPressed(ButtonId button) override;
    void onButtonHeld(ButtonId button) override;
    void render() override;

private:
    hardware::IActuator& display;
    hardware::IActuator& led;

    // View state
    uint8_t bpm;
    bool playing;
    uint32_t lastUpdateTime;
};

} // namespace ui
