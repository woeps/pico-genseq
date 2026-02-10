#include "Encoder.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "../driver/encoder.pio.h"
#include <cstdio>

namespace hardware {

Encoder::Encoder(uint8_t pinA, uint8_t pinB, PIO pio, uint sm) :
    pinA(pinA),
    pinB(pinB),
    pio(pio),
    sm(sm),
    currentValue(0),
    lastValue(-1)
{
    // Load and initialize the PIO program
    pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, pinA, 0);
}

void Encoder::update()
{
    // Get current encoder count
    currentValue = quadrature_encoder_get_count(this->pio, this->sm);

    // Check if value has changed and call callback if set
    if (currentValue != lastValue && valueChangeCallback) {
        valueChangeCallback(currentValue);
        lastValue = currentValue;
    }
}

int Encoder::getValue() const
{
    return currentValue;
}

void Encoder::setValueChangeCallback(std::function<void(int value)> callback)
{
    valueChangeCallback = callback;
}

void Encoder::setValue(int value)
{
    currentValue = value;
    lastValue = value;
    quadrature_encoder_set_count(this->pio, this->sm, value);
}

} // namespace hardware
