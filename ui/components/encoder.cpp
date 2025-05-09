#include "encoder.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "../driver/encoder.pio.h"
#include <cstdio>

namespace ui {

    // Encoder implementation
    Encoder::Encoder(uint8_t pinA, uint8_t pinB, PIO pio, uint sm, Callback callback)
        :
        pinA(pinA),
        pinB(pinB), // FIXME: this isn't used by the pio program, but pinA is just incremented by 1
        pio(pio),
        sm(sm),
        callback(callback),
        newValue(0),
        delta(0),
        oldValue(0),
        lastValue(-1),
        lastDelta(-1)
    {
        // we don't really need to keep the offset, as this program must be loaded
    // at offset 0
        pio_add_program(pio, &quadrature_encoder_program);
        quadrature_encoder_program_init(pio, sm, pinA, 0);
    }

    void Encoder::setCallback(Callback callback) {
        this->callback = callback;
    }

    void Encoder::setValue(int value) {
        this->newValue = value;
        this->oldValue = value;
        quadrature_encoder_set_count(this->pio, this->sm, value);
    }

    // FIXME: the reported values are incorrect!
    void Encoder::update() {
        // note: thanks to two's complement arithmetic delta will always
            // be correct even when new_value wraps around MAXINT / MININT
        this->newValue = quadrature_encoder_get_count(this->pio, this->sm);
        this->delta = this->newValue - this->oldValue;
        this->oldValue = this->newValue;

        if (this->newValue != this->lastValue || this->delta != this->lastDelta) {
            printf("position %8d, delta %6d\n", this->newValue, this->delta);
            this->callback(this->newValue);
            this->lastValue = this->newValue;
            this->lastDelta = this->delta;
        }
    }

} // namespace ui
