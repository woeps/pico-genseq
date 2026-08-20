#include "ui.h"
#include <cstdio>
#include "pico/time.h"

namespace ui
{
    // UI facade implementation
    UI::UI(uint8_t ledPin, uint8_t ledMatrixPin) :
        config{ledPin, ledMatrixPin}
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

    void createUITask(uint8_t ledPin, uint8_t ledMatrixPin)
    {
        printf("constructing UI facade\n");
        // Create and initialize UI with pin assignments
        UI ui(ledPin, ledMatrixPin);
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
