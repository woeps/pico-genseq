#pragma once

#include <cstdint>
#include "hardware/i2c.h"
#include <hardware/pio.h>

namespace ui {

// Hardware configuration structure
struct HardwareConfig {
    // Button pins
    uint8_t buttonEncoderPin;
    uint8_t buttonAPin;

    // Encoder pins and PIO
    uint8_t encoderPinA;
    uint8_t encoderPinB;
    PIO encoderPio;
    uint encoderSm;

    // Display configuration
    i2c_inst_t* displayI2C;
    uint8_t displayI2CAddr;
    uint8_t displaySDAPin;
    uint8_t displaySCLPin;

    // LED pin
    uint8_t ledPin;
};

} // namespace ui
