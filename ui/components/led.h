#pragma once

#include <cstdint>
#include "pico/stdlib.h"

namespace ui {

    // LED class for controlling LEDs
    class Led {
    public:
        Led(uint8_t pin);
        void on();
        void off();
        void toggle();
        void blink(uint32_t onTime, uint32_t offTime);
        void update();

    private:
        uint8_t pin;
        bool state;
        bool blinking;
        uint32_t onTime;
        uint32_t offTime;
        uint32_t lastToggleTime;
    };

} // namespace ui
