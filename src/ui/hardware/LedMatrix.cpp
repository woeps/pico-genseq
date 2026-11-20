#include "LedMatrix.h"
#include "driver/ws2812_dma.h"
#include "driver/led_matrix_pattern.h"
#include <cstring>

// Temporary BUG B part 2 isolation knob (OFF by default).
#ifndef GENSEQ_CORE1_BOOT_TRACE_NO_DMA
#define GENSEQ_CORE1_BOOT_TRACE_NO_DMA 0
#endif

namespace hardware {

LedMatrix::LedMatrix(uint8_t pin) :
    pin(pin),
    buffer{},
    dirty(false)
{
#if !GENSEQ_CORE1_BOOT_TRACE_NO_DMA
    // BUG B part 2 isolation: GENSEQ_CORE1_BOOT_TRACE_NO_DMA skips the WS2812
    // DMA/PIO/IRQ + add_alarm_in_us setup (dma_setup_init) so the rest of the
    // UI (FRAM load, restore FIFO burst, reducer/state) still runs. If core1
    // then survives, the DMA/alarm subsystem is the part-2 corruptor.
    ws2812_dma_init(pin);
#endif
}

void LedMatrix::update()
{
    if (!dirty) return;

#if !GENSEQ_CORE1_BOOT_TRACE_NO_DMA
    ws2812_put_pixels(buffer);
    ws2812_dma_handle();
#endif
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

void LedMatrix::drawValueText(const char (&text)[4], uint32_t color)
{
    get_value_text_pattern(&text, &buffer, &color);
    dirty = true;
}

void LedMatrix::drawNote(const char (&noteStr)[4], uint32_t color)
{
    get_note_pattern(&noteStr, &buffer, &color);
    dirty = true;
}

} // namespace hardware
