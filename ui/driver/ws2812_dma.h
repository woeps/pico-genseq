#ifndef WS2812_DMA_H
#define WS2812_DMA_H

#include <pico/types.h>
#include "pico/assert.h"

#define NUM_PIXELS 256

// Helper to create 32-bit GRB color
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}

// Helper to create 32-bit GRB color with brightness (0-255)
static inline uint32_t urgb_u32_dimmed(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    hard_assert(brightness <= 255 && brightness >= 0);
    // Scale each channel by the brightness factor
    // (val * brightness) >> 8 is equivalent to (val * brightness) / 256
    return urgb_u32(
        (uint8_t)((uint16_t)r * brightness >> 8),
        (uint8_t)((uint16_t)g * brightness >> 8),
        (uint8_t)((uint16_t)b * brightness >> 8)
    );
}

/**
 * @brief Initialize the WS2812 DMA driver
 * 
 * @param pin The GPIO pin connected to the LED strip data line
 */
void ws2812_dma_init(uint pin);

/**
 * @brief Add a pixel to the internal buffer
 * 
 * @param pixel_grb The 32-bit GRB color value
 */
void ws2812_put_pixel(uint32_t pixel_grb);

/**
 * @brief  Replace the content of the internal buffer with the given array
 * 
 * @param pixels Array of <NUM_PIXELS> 32-bit GRB color values
 */
void ws2812_put_pixels(uint32_t *pixels);

/**
 * @brief Reset the internal pixel index to 0
 */
void ws2812_reset_pixel_index(void);

/**
 * @brief Trigger a DMA transfer to update the LEDs
 * 
 * This function initiates the transfer of the buffer to the PIO via DMA.
 * It does not block. Use ws2812_dma_ready() to check if the next transfer can start.
 */
void ws2812_dma_trigger(void);

/**
 * @brief Check if the driver is ready for a new transfer
 * 
 * @return true if the previous transfer (plus reset delay) is complete
 * @return false if the driver is busy
 */
bool ws2812_dma_ready(void);

/**
 * @brief Handle DMA transfer (trigger if ready)
 * 
 * Checks if the DMA is ready and triggers a new transfer if so.
 */
void ws2812_dma_handle(void);

#endif // WS2812_DMA_H
