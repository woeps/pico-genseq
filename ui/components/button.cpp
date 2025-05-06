#include "button.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

namespace ui
{

    Button::Button(uint8_t pin, Callback pressCallback, Callback releaseCallback, Callback holdCallback, uint32_t holdTimeMs) :
        pin(pin),
        pressed(false),
        pressCallback(pressCallback),
        releaseCallback(releaseCallback),
        holdCallback(holdCallback),
        holdTimeMs(holdTimeMs),
        pressStartTime(0),
        holdTriggered(false),
        lastDebounceTime(0),
        lastState(false)
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
                    // Don't trigger press callback immediately - wait to see if it's a hold
                }
                else
                {
                    // Button was just released
                    if (!holdTriggered && pressCallback)
                    {
                        // If we didn't trigger the hold callback, trigger the press callback
                        pressCallback();
                    }

                    if (releaseCallback)
                    {
                        releaseCallback();
                    }
                }
            }

            // Check for hold event if button is still pressed
            if (pressed && holdCallback && !holdTriggered && (currentTime - pressStartTime) >= holdTimeMs)
            {
                holdCallback();
                holdTriggered = true; // Prevent multiple triggers of the hold callback
            }
        }

        lastState = reading;
    }

} // namespace ui
