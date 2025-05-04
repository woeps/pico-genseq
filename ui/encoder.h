#pragma once

#include <cstdint>
#include <functional>
#include "pico/stdlib.h"

namespace ui {

// Encoder class for handling rotary encoder inputs
class Encoder {
public:
    using Callback = std::function<void(int)>;
    
    Encoder(uint8_t pinA, uint8_t pinB, Callback callback = nullptr);
    void init();
    int getValue() const;
    void setValue(int value);
    void setCallback(Callback callback);

private:
    uint8_t pinA;
    uint8_t pinB;
    int value;
    Callback callback;
    uint8_t lastState;
    
    // GPIO interrupt handler
    static void encoderIRQHandler(uint gpio, uint32_t events);
    
    // Static instance for IRQ handler
    static Encoder* instance;
};

} // namespace ui
