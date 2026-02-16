#include "Potentiometer.h"
#include "hardware/adc.h"
#include "../Event.h"
#include "../state/StateManager.h"
#include <cstdio>

namespace hardware {

Potentiometer::Potentiometer(uint8_t pin, ui::PotId potId) :
    pin(pin),
    adcInput(pin - 26),
    potId(potId),
    currentValue(0),
    smoothedValue(0)
{
    adc_init();
    adc_gpio_init(pin);
    gpio_disable_pulls(pin);
}

void Potentiometer::update()
{
    adc_select_input(adcInput);
    uint16_t reading = adc_read();

    if (smoothedValue == 0) {
        smoothedValue = static_cast<uint32_t>(reading) << SMOOTHING_SHIFT;
    } else {
        smoothedValue += reading - (smoothedValue >> SMOOTHING_SHIFT);
    }
    uint16_t filtered = static_cast<uint16_t>(smoothedValue >> SMOOTHING_SHIFT);

    printf("raw=%d filtered=%d\n", reading, filtered);

    int16_t diff = static_cast<int16_t>(filtered) - static_cast<int16_t>(currentValue);
    if (diff < 0) diff = -diff;

    if (static_cast<uint16_t>(diff) >= CHANGE_THRESHOLD)
    {
        currentValue = filtered;
        ui::events::Event event = ui::events::Event::potChanged(potId, currentValue);
        ui::state::getStateManager().dispatch(event);
    }
}

} // namespace hardware
