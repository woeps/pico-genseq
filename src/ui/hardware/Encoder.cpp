#include "Encoder.h"
#include "driver/encoder.pio.h"

namespace hardware {

Encoder::Encoder(uint8_t pinA, uint8_t pinB, PIO pio, uint sm) :
    pinA(pinA),
    pinB(pinB),
    pio(pio),
    sm(sm),
    currentValue(0),
    lastValue(-1)
{
    pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, pinA, 0);
}

void Encoder::update()
{
    currentValue = quadrature_encoder_get_count(this->pio, this->sm);

    if (currentValue != lastValue) {
        lastValue = currentValue;
    }
}

int Encoder::getValue() const
{
    return currentValue;
}

void Encoder::setValue(int value)
{
    currentValue = value;
    lastValue = value;
    quadrature_encoder_set_count(this->pio, this->sm, value);
}

} // namespace hardware
