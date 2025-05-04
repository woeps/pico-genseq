#include "encoder.h"
#include "hardware/gpio.h"

namespace ui {

// Static instance for encoder IRQ handler
Encoder* Encoder::instance = nullptr;

// Encoder implementation
Encoder::Encoder(uint8_t pinA, uint8_t pinB, Callback callback)
    : pinA(pinA), pinB(pinB), value(0), callback(callback) {
    instance = this;
}

void Encoder::init() {
    // Initialize GPIO pins
    gpio_init(pinA);
    gpio_init(pinB);
    gpio_set_dir(pinA, GPIO_IN);
    gpio_set_dir(pinB, GPIO_IN);
    gpio_pull_up(pinA);
    gpio_pull_up(pinB);
    
    // Read initial state
    lastState = (gpio_get(pinA) << 1) | gpio_get(pinB);
    
    // Set up interrupts for encoder pins
    gpio_set_irq_enabled_with_callback(pinA, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &encoderIRQHandler);
    gpio_set_irq_enabled(pinB, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}

int Encoder::getValue() const {
    return value;
}

void Encoder::setValue(int value) {
    this->value = value;
}

void Encoder::setCallback(Callback callback) {
    this->callback = callback;
}

void Encoder::encoderIRQHandler(uint gpio, uint32_t events) {
    if (!instance) return;
    
    // Read the current state of both pins
    uint8_t currentState = (gpio_get(instance->pinA) << 1) | gpio_get(instance->pinB);
    uint8_t lastState = instance->lastState;
    
    // Determine direction based on state transition
    // This implements a simple state machine for quadrature decoding
    // The state transitions form a Gray code pattern
    if ((lastState == 0b00 && currentState == 0b01) ||
        (lastState == 0b01 && currentState == 0b11) ||
        (lastState == 0b11 && currentState == 0b10) ||
        (lastState == 0b10 && currentState == 0b00)) {
        // Clockwise rotation
        instance->value++;
        if (instance->callback) {
            instance->callback(1);
        }
    } else if ((lastState == 0b00 && currentState == 0b10) ||
               (lastState == 0b10 && currentState == 0b11) ||
               (lastState == 0b11 && currentState == 0b01) ||
               (lastState == 0b01 && currentState == 0b00)) {
        // Counter-clockwise rotation
        instance->value--;
        if (instance->callback) {
            instance->callback(-1);
        }
    }
    
    // Update the last state
    instance->lastState = currentState;
}

} // namespace ui
