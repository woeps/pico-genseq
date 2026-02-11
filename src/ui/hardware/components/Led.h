#pragma once

#include <cstdint>
#include "../interfaces/IOutput.h"

namespace hardware {

class Led : public IOutput {
public:
    Led(uint8_t pin);

    void update() override;

    void setValue(int value) override;

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
