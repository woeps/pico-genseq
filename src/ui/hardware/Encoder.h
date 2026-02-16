#pragma once

#include <cstdint>
#include <hardware/pio.h>

namespace hardware {

class Encoder {
public:
    Encoder(uint8_t pinA, uint8_t pinB, PIO pio, uint sm);

    void update();

    int getValue() const;
    void setValue(int value);

private:
    uint8_t pinA;
    uint8_t pinB;
    PIO pio;
    uint sm;

    int currentValue;
    int lastValue;
};

} // namespace hardware
