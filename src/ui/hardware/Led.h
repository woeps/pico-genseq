#pragma once

#include <cstdint>

namespace hardware {

class Led {
public:
    Led(uint8_t pin);

    void update();

    void on();
    void off();
    void toggle();
    void blink(uint32_t onTime, uint32_t offTime);

private:
    uint8_t pin;
    bool state;
    bool blinking;
    uint32_t onTime;
    uint32_t offTime;
    uint32_t lastToggleTime;
};

} // namespace hardware
