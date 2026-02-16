#include "ui.h"
#include <cstdio>
#include "pico/time.h"

namespace ui
{
    // UI facade implementation
    UI::UI(
        const uint8_t (&buttonPins)[6],
        uint8_t ledPin,
        uint8_t ledMatrixPin,
        uint8_t potPin) :
        config{
            {buttonPins[0], buttonPins[1], buttonPins[2],
             buttonPins[3], buttonPins[4], buttonPins[5]},
            ledPin,
            ledMatrixPin,
            potPin
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
        const uint8_t (&buttonPins)[6],
        uint8_t ledPin,
        uint8_t ledMatrixPin,
        uint8_t potPin)
    {
        printf("constructing UI facade\n");
        // Create and initialize UI with pin assignments
        UI ui(
            buttonPins,
            ledPin,
            ledMatrixPin,
            potPin);
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
