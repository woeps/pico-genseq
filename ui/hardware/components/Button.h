#pragma once

#include <cstdint>
#include <functional>
#include "../interfaces/ISensor.h"

namespace hardware {

class Button : public ISensor {
public:
    Button(uint8_t pin);

    void update() override;
    void setValueChangeCallback(std::function<void(int)> callback) override;

private:
    uint8_t pin;
    bool pressed;
    bool holdTriggered;
    uint32_t pressStartTime;
    uint32_t lastDebounceTime;
    bool lastState;
    uint32_t holdTimeMs;

    std::function<void(int)> valueChangeCallback;
};

} // namespace hardware
