// No-op Led / LedMatrix bodies. Led.h and LedMatrix.h are Pico-free, so views
// that hold references to them compile and link on the host against these.
#include "ui/hardware/Led.h"
#include "ui/hardware/LedMatrix.h"

namespace hardware {

Led::Led(uint8_t pin)
    : pin(pin), state(false), blinking(false),
      onTime(0), offTime(0), lastToggleTime(0) {}
void Led::update() {}
void Led::on() {}
void Led::off() {}
void Led::toggle() {}
void Led::blink(uint32_t, uint32_t) {}

LedMatrix::LedMatrix(uint8_t pin) : pin(pin), buffer{}, dirty(false) {}
void LedMatrix::update() {}
void LedMatrix::clear() {}
void LedMatrix::setPixel(uint8_t, uint8_t, uint32_t) {}
void LedMatrix::fill(uint32_t) {}
void LedMatrix::drawNumber(int, uint32_t) {}
void LedMatrix::drawLabel(const char (&)[4], uint32_t) {}
void LedMatrix::drawNote(const char (&)[3], uint32_t) {}

} // namespace hardware
