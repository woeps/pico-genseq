// Host LedMatrix that keeps the real pixel buffer, using the firmware's own
// glyph code (led_matrix_pattern.h), so manual screenshots are the real rendering.
#define private public  // tooling only: expose the pixel buffer
#include "ui/hardware/Led.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/hardware/driver/led_matrix_pattern.h"
#undef private
#include "capture.h"
#include <cstring>

namespace hardware {
Led::Led(uint8_t pin) : pin(pin), state(false), blinking(false), onTime(0), offTime(0), lastToggleTime(0) {}
void Led::update() {}
void Led::on() {}
void Led::off() {}
void Led::toggle() {}
void Led::blink(uint32_t, uint32_t) {}

LedMatrix::LedMatrix(uint8_t pin) : pin(pin), buffer{}, dirty(false) {}
void LedMatrix::update() {}
void LedMatrix::clear() { memset(buffer, 0, sizeof(buffer)); }
void LedMatrix::setPixel(uint8_t x, uint8_t y, uint32_t c) { if (x < WIDTH && y < HEIGHT) buffer[y * WIDTH + x] = c; }
void LedMatrix::fill(uint32_t c) { for (auto& p : buffer) p = c; }
void LedMatrix::drawNumber(int n, uint32_t c) { get_number_pattern(&n, &buffer, &c); }
void LedMatrix::drawLabel(const char (&t)[4], uint32_t c) { get_label_pattern(&t, &buffer, &c); }
void LedMatrix::drawValueText(const char (&t)[4], uint32_t c) { get_value_text_pattern(&t, &buffer, &c); }
void LedMatrix::drawNote(const char (&t)[4], uint32_t c) { get_note_pattern(&t, &buffer, &c); }
const uint32_t* capturePixels(const LedMatrix& m) { return m.buffer; }
}
