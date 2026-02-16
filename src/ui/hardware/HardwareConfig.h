#pragma once

#include <cstdint>

namespace ui {

// Hardware configuration structure
struct HardwareConfig {
    // Button pins
    uint8_t buttonPins[6];

    // LED pin
    uint8_t ledPin;

    // LED matrix pin
    uint8_t ledMatrixPin;

    // Potentiometer pin (ADC)
    uint8_t potPin;
};

} // namespace ui
