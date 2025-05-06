#include "led.h"
#include "hardware/gpio.h"
#include <cstdio>

namespace ui {

Led::Led(uint8_t pin) :
    pin(pin),
    state(false),
    blinking(false),
    onTime(0),
    offTime(0),
    lastToggleTime(0)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    off();
}

void Led::on() {
    gpio_put(pin, 1);
    state = true;
    blinking = false;
}

void Led::off() {
    gpio_put(pin, 0);
    state = false;
    blinking = false;
}

void Led::toggle() {
    state = !state;
    gpio_put(pin, state);
}

/**
 * @brief Start blinking the LED.
 *
 * The LED will be turned on for `onTime` milliseconds and then turned off for
 * `offTime` milliseconds. The blinking will continue until `stopBlinking` is called.
 *
 * @param onTime The time in milliseconds the LED should be on.
 * @param offTime The time in milliseconds the LED should be off.
 */
void Led::blink(uint32_t onTime, uint32_t offTime) {
    this->onTime = onTime;
    this->offTime = offTime;
    this->blinking = true;
    lastToggleTime = to_ms_since_boot(get_absolute_time());
    gpio_put(pin, 1);
    this->state = true;
}

void Led::update() {
    if (!blinking) return;
    
    uint32_t currentTime = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed = currentTime - lastToggleTime;
    
    if ((state && elapsed >= onTime) || (!state && elapsed >= offTime)) {
        toggle();
        lastToggleTime = currentTime;
    }
}

} // namespace ui
