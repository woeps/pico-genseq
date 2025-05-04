#pragma once

#include <cstdint>
#include <functional>
#include "pico/stdlib.h"

namespace ui {

// Button class for handling button inputs
class Button {
public:
    using Callback = std::function<void(void)>;
    
    Button(uint8_t pin, Callback pressCallback = nullptr, Callback releaseCallback = nullptr);
    void init();
    void update();
    bool isPressed() const;

private:
    uint8_t pin;
    bool pressed;
    Callback pressCallback;
    Callback releaseCallback;
    uint32_t lastDebounceTime;
    bool lastState;
};

} // namespace ui
