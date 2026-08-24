// No-op Led / LedMatrix bodies. Led.h and LedMatrix.h are Pico-free, so views
// that hold references to them compile and link on the host against these.
#include "ui/hardware/Led.h"
#include "ui/hardware/LedMatrix.h"
#include "hardware_stub.h"

namespace hardware {
namespace {

uint32_t matrixPixels[LedMatrix::NUM_PIXELS]{};
char matrixLabel[4]{};

}

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
void LedMatrix::clear() {
    for (auto& pixel : matrixPixels) pixel = 0;
}
void LedMatrix::setPixel(uint8_t x, uint8_t y, uint32_t color) {
    if (x < WIDTH && y < HEIGHT) matrixPixels[y * WIDTH + x] = color;
}
void LedMatrix::fill(uint32_t color) {
    for (auto& pixel : matrixPixels) pixel = color;
}
void LedMatrix::drawNumber(int, uint32_t) {}
void LedMatrix::drawLabel(const char (&text)[4], uint32_t) {
    for (uint8_t i = 0; i < 4; i++) matrixLabel[i] = text[i];
}
void LedMatrix::drawNote(const char (&)[3], uint32_t) {}

namespace testing {

void resetMatrix() {
    for (auto& pixel : matrixPixels) pixel = 0;
    for (auto& character : matrixLabel) character = 0;
}

uint32_t pixelAt(uint8_t x, uint8_t y) {
    return x < LedMatrix::WIDTH && y < LedMatrix::HEIGHT
        ? matrixPixels[y * LedMatrix::WIDTH + x]
        : 0;
}

const char* lastLabel() {
    return matrixLabel;
}

}

} // namespace hardware
