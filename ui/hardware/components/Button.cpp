#include "Button.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

namespace hardware {

Button::Button(uint8_t pin) :
    pin(pin),
    pressed(false),
    holdTriggered(false),
    pressStartTime(0),
    lastDebounceTime(0),
    lastState(false),
    holdTimeMs(1000)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

void Button::update()
{
    bool reading = !gpio_get(pin); // Inverted because of pull-up
    uint32_t currentTime = to_ms_since_boot(get_absolute_time());

    // Check if the button state has changed
    if (reading != lastState)
    {
        lastDebounceTime = currentTime;
    }

    // If the button state has been stable for 50ms, update the button state
    if ((currentTime - lastDebounceTime) > 50)
    {
        if (reading != pressed)
        {
            pressed = reading;

            if (pressed)
            {
                // Button was just pressed
                pressStartTime = currentTime;
                holdTriggered = false;
                if (valueChangeCallback) {
                    valueChangeCallback(1); // 1 = pressed
                }
            }
            else
            {
                // Button was just released
                if (valueChangeCallback) {
                    valueChangeCallback(0); // 0 = released
                }
            }
        }

        // Check for hold event if button is still pressed
        if (pressed && !holdTriggered && (currentTime - pressStartTime) >= holdTimeMs)
        {
            holdTriggered = true;
            if (valueChangeCallback) {
                valueChangeCallback(2); // 2 = held
            }
        }
    }

    lastState = reading;
}

void Button::setValueChangeCallback(std::function<void(int)> callback)
{
    valueChangeCallback = callback;
}

} // namespace hardware
