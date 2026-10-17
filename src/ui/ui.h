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
        UI(uint8_t ledPin, uint8_t ledMatrixPin);

        void init();
        void update();

    private:
        HardwareConfig config;
        std::unique_ptr<UIController> controller;
    };

    // Function to create the UI task for the first core
    void createUITask(uint8_t ledPin, uint8_t ledMatrixPin);

} // namespace ui
