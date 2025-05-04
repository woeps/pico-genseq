#include "button.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

namespace ui {

Button::Button(uint8_t pin, Callback pressCallback, Callback releaseCallback)
    : pin(pin), pressed(false), pressCallback(pressCallback), releaseCallback(releaseCallback),
      lastDebounceTime(0), lastState(false) {}

void Button::init() {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

void Button::update() {
    bool reading = !gpio_get(pin); // Inverted because of pull-up
    
    // Check if the button state has changed
    if (reading != lastState) {
        lastDebounceTime = to_ms_since_boot(get_absolute_time());
    }
    
    // If the button state has been stable for 50ms, update the button state
    if ((to_ms_since_boot(get_absolute_time()) - lastDebounceTime) > 50) {
        if (reading != pressed) {
            pressed = reading;
            
            if (pressed && pressCallback) {
                pressCallback();
            } else if (!pressed && releaseCallback) {
                releaseCallback();
            }
        }
    }
    
    lastState = reading;
}

bool Button::isPressed() const {
    return pressed;
}

} // namespace ui
