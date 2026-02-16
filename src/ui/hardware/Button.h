#pragma once

#include <cstdint>
#include "../Types.h"

namespace hardware {

class Button {
public:
    Button(uint8_t pin, ui::ButtonId buttonId);

    void update();

private:
    uint8_t pin;
    ui::ButtonId buttonId;
    bool pressed;
    bool holdTriggered;
    uint32_t pressStartTime;
    uint32_t lastDebounceTime;
    bool lastState;
    uint32_t holdTimeMs;
};

} // namespace hardware
