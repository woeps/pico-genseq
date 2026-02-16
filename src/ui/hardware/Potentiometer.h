#pragma once

#include <cstdint>
#include "../Types.h"

namespace hardware {

class Potentiometer {
public:
    Potentiometer(uint8_t pin, ui::PotId potId);

    void update();

    uint16_t getValue() const { return currentValue; }

private:
    uint8_t pin;
    uint8_t adcInput;
    ui::PotId potId;
    uint16_t currentValue;
    uint32_t smoothedValue;
    static constexpr uint16_t CHANGE_THRESHOLD = 4;
    static constexpr uint8_t SMOOTHING_SHIFT = 2;
};

} // namespace hardware
