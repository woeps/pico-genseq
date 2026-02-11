#pragma once

#include <cstdint>
#include <memory>
#include "controllers/HardwareConfig.h"
#include "controllers/UIController.h"

namespace ui
{
    // Main UI facade class - provides simplified interface to the new UI architecture
    class UI
    {
    public:
        // Constructor with pin assignments
        UI(
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
            uint8_t ledPin);
        
        void init();
        void update();

    private:
        HardwareConfig config;
        std::unique_ptr<UIController> controller;
    };

    // Function to create the UI task for the first core
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
        uint8_t ledPin);

} // namespace ui
