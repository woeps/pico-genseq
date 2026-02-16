#pragma once

#include <cstdint>
#include <memory>
#include "UIController.h"

namespace ui
{
    // Main UI facade class - provides simplified interface to the new UI architecture
    class UI
    {
    public:
        // Constructor with pin assignments
        UI(
            const uint8_t (&buttonPins)[6],
            uint8_t ledPin,
            uint8_t ledMatrixPin,
            uint8_t potPin);
        
        void init();
        void update();

    private:
        HardwareConfig config;
        std::unique_ptr<UIController> controller;
    };

    // Function to create the UI task for the first core
    void createUITask(
        const uint8_t (&buttonPins)[6],
        uint8_t ledPin,
        uint8_t ledMatrixPin,
        uint8_t potPin);

} // namespace ui
