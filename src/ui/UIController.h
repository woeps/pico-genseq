#pragma once

#include <array>
#include <memory>
#include "hardware/HardwareConfig.h"
#include "hardware/Button.h"
#include "hardware/Potentiometer.h"
#include "hardware/Led.h"
#include "hardware/LedMatrix.h"
#include "views/IView.h"
#include "views/InitView.h"
#include "views/SettingsView.h"
#include "state/UIState.h"

namespace ui {

class UIController {
public:
    UIController(const HardwareConfig& config);
    ~UIController();

    void initialize();
    void update();

private:
    const HardwareConfig& config;

    // Hardware
    std::array<std::unique_ptr<hardware::Button>, BUTTON_COUNT> buttons;
    std::unique_ptr<hardware::Potentiometer> pot;
    std::unique_ptr<hardware::Led> led;
    std::unique_ptr<hardware::LedMatrix> ledMatrix;

    // Views (heap-allocated but fixed at initialization, no dynamic allocation after)
    std::unique_ptr<InitView> initView;
    std::unique_ptr<SettingsView> settingsView;
    std::array<IView*, static_cast<size_t>(state::ViewId::SETTINGS) + 1> views;
    IView* activeView;

    void onStateChanged(const state::UIState& newState);
};

} // namespace ui
