#include "Button.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "../Event.h"
#include "../state/StateManager.h"

namespace hardware {

Button::Button(uint8_t pin, ui::ButtonId buttonId) :
    pin(pin),
    buttonId(buttonId),
    pressed(false),
    holdTriggered(false),
    pressPending(false),
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
    bool reading = !gpio_get(pin);
    uint32_t currentTime = to_ms_since_boot(get_absolute_time());

    if (reading != lastState)
    {
        lastDebounceTime = currentTime;
    }

    if ((currentTime - lastDebounceTime) > 50)
    {
        if (reading != pressed)
        {
            pressed = reading;

            if (pressed)
            {
                pressStartTime = currentTime;
                holdTriggered = false;
                pressPending = true;
            }
            else
            {
                if (pressPending)
                {
                    ui::events::Event pressEvent = ui::events::Event::buttonPressed(buttonId);
                    ui::state::getStateManager().dispatch(pressEvent);
                }

                ui::events::Event event = ui::events::Event::buttonReleased(buttonId);
                ui::state::getStateManager().dispatch(event);
                pressPending = false;
            }
        }
    }

    // Hold detection runs after debounce but independent of state changes
    if (pressed && !holdTriggered && (currentTime - pressStartTime) >= holdTimeMs)
    {
        holdTriggered = true;
        pressPending = false;
        ui::events::Event event = ui::events::Event::buttonHeld(buttonId);
        ui::state::getStateManager().dispatch(event);
    }

    lastState = reading;
}

} // namespace hardware
