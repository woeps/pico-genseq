#pragma once

#include <memory>
#include <array>
#include "HardwareConfig.h"
#include "../hardware/interfaces/ISensor.h"
#include "../hardware/interfaces/IActuator.h"
#include "../navigation/ViewManager.h"

namespace ui {

// UI Controller that manages sensors, actuators, and view navigation
class UIController {
public:
    UIController(const HardwareConfig& config);
    ~UIController();

    void initialize();
    void update();

    // Navigation methods
    void navigateToMainView();
    void navigateToSettingsView();

private:
    const HardwareConfig& config;

    // View manager
    std::unique_ptr<ViewManager> viewManager;

    // Sensors (input devices)
    std::array<std::unique_ptr<hardware::ISensor>, 2> buttons;
    std::unique_ptr<hardware::ISensor> encoder;

    // Actuators (output devices)
    std::unique_ptr<hardware::IActuator> display;
    std::unique_ptr<hardware::IActuator> led;

    // Hardware initialization
    void setupSensors();
    void setupActuators();
    void setupViews();

    // Input handling
    void handleSensorInput();
    void handleButtonValueChanged(int index, int value);
    void handleEncoderValueChanged(int value);
};

} // namespace ui
