#pragma once

#include <cstdint>
#include <functional>
#include <hardware/pio.h>
#include "../interfaces/IInput.h"

namespace hardware {

class Encoder : public IInput {
public:
    Encoder(uint8_t pinA, uint8_t pinB, PIO pio, uint sm);

    void update() override;
    void setValueChangeCallback(std::function<void(int)> callback) override;

    int getValue() const;
    void setValue(int value);

private:
    uint8_t pinA;
    uint8_t pinB;
    PIO pio;
    uint sm;

    int currentValue;
    int lastValue;

    std::function<void(int)> valueChangeCallback;
};

} // namespace hardware
