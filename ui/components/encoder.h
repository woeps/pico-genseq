#pragma once

#include <cstdint>
#include <functional>
#include "pico/stdlib.h"
#include <hardware/pio.h>

namespace ui {

// Encoder class for handling rotary encoder inputs
class Encoder {
public:
    /// Callback function type for when the encoder value changes
    ///
    /// \param delta The change in value of the encoder. This will always increase or
    /// decrease by one for each clockwise or anti-clockwise "step" of the encoder.
    using Callback = std::function<void(int delta)>;
    
    /// Create an encoder object for handling rotary encoder inputs
    ///
    /// \param pinA First encoder pin
    /// \param pinB Second encoder pin
    /// \param pio PIO instance to use
    /// \param sm State machine to use
    /// \param callback Optional callback to be called when the encoder value changes
    Encoder(uint8_t pinA, uint8_t pinB, PIO pio, uint sm, Callback callback = nullptr);

    /// Set the callback to be called when the encoder value changes
    ///
    /// \param callback Optional callback to be called when the encoder value changes
    void setCallback(Callback callback = nullptr);

    /// Update the encoder state. This should be called periodically, e.g. in the main loop.
    ///
    /// This function will check if the encoder has changed and if so, call the callback
    /// function with the change in value. The callback function will be called with the
    /// change in value, i.e. 1 for a clockwise "step" and -1 for an anti-clockwise "step".
    void update();

private:
    uint8_t pinA;
    uint8_t pinB;
    PIO pio;
    uint sm;
    Callback callback;

    int newValue;
    int delta;
    int oldValue;
    int lastValue;
    int lastDelta;
};

} // namespace ui
