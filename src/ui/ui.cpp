#include "ui.h"
#include <cstdio>

namespace ui
{
    // UI facade implementation
    UI::UI(
        uint8_t buttonEncoderPin,
        uint8_t buttonAPin,
        uint8_t encoderPinA,
        uint8_t encoderPinB,
        PIO encoderPio,
        uint encoderSm,
        i2c_inst_t* displayI2C,
        uint8_t displayI2CAddr,
        uint8_t displaySDAPin,
        uint8_t displaySCLPin,
        uint8_t ledPin) :
        config{
            buttonEncoderPin,
            buttonAPin,
            encoderPinA,
            encoderPinB,
            encoderPio,
            encoderSm,
            displayI2C,
            displayI2CAddr,
            displaySDAPin,
            displaySCLPin,
            ledPin
        }
    {
        controller = std::make_unique<UIController>(config);
    }

    void UI::init()
    {
        printf("Initializing UI facade...\n");
        controller->initialize();
        printf("UI facade initialized\n");
    }

    void UI::update()
    {
        controller->update();
    }

    void createUITask(
        uint8_t buttonEncoderPin,
        uint8_t buttonAPin,
        uint8_t encoderPinA,
        uint8_t encoderPinB,
        PIO encoderPio,
        uint encoderSm,
        i2c_inst_t* displayI2C,
        uint8_t displayI2CAddr,
        uint8_t displaySDAPin,
        uint8_t displaySCLPin,
        uint8_t ledPin)
    {
        printf("constructing UI facade\n");
        // Create and initialize UI with pin assignments
        UI ui(
            buttonEncoderPin,
            buttonAPin,
            encoderPinA,
            encoderPinB,
            encoderPio,
            encoderSm,
            displayI2C,
            displayI2CAddr,
            displaySDAPin,
            displaySCLPin,
            ledPin);
        printf("initializing UI facade\n");
        ui.init();

        // Main UI loop
        while (true)
        {
            // Update UI components
            ui.update();

            // Small delay to prevent tight looping
            sleep_ms(1);
        }
    }

} // namespace ui
