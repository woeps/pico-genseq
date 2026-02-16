#include "LedMatrix.h"
#include "driver/ws2812_dma.h"
#include "driver/led_matrix_pattern.h"
#include <cstring>

namespace hardware {

LedMatrix::LedMatrix(uint8_t pin) :
    pin(pin),
    buffer{},
    dirty(false)
{
    ws2812_dma_init(pin);
}

void LedMatrix::update()
{
    if (!dirty) return;

    ws2812_put_pixels(buffer);
    ws2812_dma_handle();
    dirty = false;
}

void LedMatrix::clear()
{
    memset(buffer, 0, sizeof(buffer));
    dirty = true;
}

void LedMatrix::setPixel(uint8_t x, uint8_t y, uint32_t color)
{
    if (x >= WIDTH || y >= HEIGHT) return;
    buffer[y * WIDTH + x] = color;
    dirty = true;
}

void LedMatrix::fill(uint32_t color)
{
    for (uint16_t i = 0; i < NUM_PIXELS; i++) {
        buffer[i] = color;
    }
    dirty = true;
}

void LedMatrix::drawNumber(int number, uint32_t color)
{
    get_number_pattern(&number, &buffer, &color);
    dirty = true;
}

void LedMatrix::drawLabel(const char (&text)[4], uint32_t color)
{
    get_label_pattern(&text, &buffer, &color);
    dirty = true;
}

void LedMatrix::drawNote(const char (&noteStr)[3], uint32_t color)
{
    get_note_pattern(&noteStr, &buffer, &color);
    dirty = true;
}

} // namespace hardware
