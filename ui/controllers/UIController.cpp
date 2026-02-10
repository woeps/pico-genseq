#include "UIController.h"
#include "../hardware/components/Button.h"
#include "../hardware/components/Encoder.h"
#include "../hardware/components/Led.h"
#include "../hardware/components/Display.h"
#include <cstdio>

namespace ui {

UIController::UIController(const HardwareConfig& config) : config(config) {}

UIController::~UIController() = default;

void UIController::initialize()
{
    printf("Initializing UI Controller...\n");

    setupSensors();
    setupActuators();
    setupViews();

    printf("UI Controller initialized\n");
}

void UIController::update()
{
    // Update all sensors
    for (auto& button : buttons) {
        if (button) button->update();
    }
    if (encoder) encoder->update();

    // Update all actuators
    if (display) display->update();
    if (led) led->update();

    // Update view manager
    if (viewManager) viewManager->update();

    // Handle input
    handleSensorInput();
}

void UIController::navigateToMainView()
{
    // TODO: Implement navigation to main view
    printf("Navigate to main view\n");
}

void UIController::navigateToSettingsView()
{
    // TODO: Implement navigation to settings view
    printf("Navigate to settings view\n");
}

void UIController::setupSensors()
{
    printf("Setting up sensors...\n");

    // Create buttons
    buttons[0] = std::make_unique<hardware::Button>(config.buttonEncoderPin);
    buttons[1] = std::make_unique<hardware::Button>(config.buttonAPin);

    // Set up button callbacks
    buttons[0]->setValueChangeCallback([this](int value) {
        handleButtonValueChanged(0, value); // Encoder button
    });
    buttons[1]->setValueChangeCallback([this](int value) {
        handleButtonValueChanged(1, value); // Button A
    });

    // Create encoder
    encoder = std::make_unique<hardware::Encoder>(
        config.encoderPinA,
        config.encoderPinB,
        config.encoderPio,
        config.encoderSm
    );
    encoder->setValueChangeCallback([this](int value) {
        handleEncoderValueChanged(value);
    });

    printf("Sensors setup complete\n");
}

void UIController::setupActuators()
{
    printf("Setting up actuators...\n");

    // Create display
    display = std::make_unique<hardware::Display>(
        config.displayI2C,
        config.displayI2CAddr,
        config.displaySDAPin,
        config.displaySCLPin
    );

    // Create LED
    led = std::make_unique<hardware::Led>(config.ledPin);

    printf("Actuators setup complete\n");
}

void UIController::setupViews()
{
    printf("Setting up views...\n");

    viewManager = std::make_unique<ViewManager>();

    // TODO: Create and push initial view
    // viewManager->pushView(std::make_unique<MainView>(...));

    printf("Views setup complete\n");
}

void UIController::handleSensorInput()
{
    // Input handling is done through callbacks, no additional processing needed here
}

void UIController::handleButtonValueChanged(int index, int value)
{
    ButtonId buttonId = (index == 0) ? ButtonId::ENCODER_BUTTON : ButtonId::BUTTON_A;

    if (value == 1) { // Pressed
        if (viewManager) {
            viewManager->handleButtonPressed(buttonId);
        }
    } else if (value == 2) { // Held
        if (viewManager) {
            viewManager->handleButtonHeld(buttonId);
        }
    }
    // value == 0 is released, no action needed
}

void UIController::handleEncoderValueChanged(int value)
{
    static int lastValue = 0;
    int delta = value - lastValue;
    lastValue = value;

    if (viewManager && delta != 0) {
        viewManager->handleEncoderChanged(delta);
    }
}

} // namespace ui
