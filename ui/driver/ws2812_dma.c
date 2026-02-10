// #include <stdio.h>
#include <string.h>
#include "pico/sem.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "ws2812.pio.h"
#include "ws2812_dma.h"

#define NUM_PIXELS 256

// DMA channels
#define DMA_CHANNEL 0
#define DMA_CB_CHANNEL 1
#define DMA_CHANNEL_MASK (1u << DMA_CHANNEL)
#define DMA_CB_CHANNEL_MASK (1u << DMA_CB_CHANNEL)
#define DMA_CHANNELS_MASK (DMA_CHANNEL_MASK | DMA_CB_CHANNEL_MASK)

// Driver state
static uint32_t led_data[NUM_PIXELS];
static uint led_index;
static struct semaphore reset_delay_complete_sem;
static alarm_id_t reset_delay_alarm_id;
static PIO ws2812_pio;
static uint ws2812_sm;
static uint ws2812_offset;

// Callback for the reset delay alarm
static int64_t reset_delay_complete(__unused alarm_id_t id, __unused void *user_data) {
    reset_delay_alarm_id = 0;
    sem_release(&reset_delay_complete_sem);
    return 0;
}

// DMA complete interrupt handler
static void __isr dma_complete_handler() {
    if (dma_hw->ints0 & DMA_CHANNEL_MASK) {
        dma_hw->ints0 = DMA_CHANNEL_MASK;
        if (reset_delay_alarm_id) cancel_alarm(reset_delay_alarm_id);
        reset_delay_alarm_id = add_alarm_in_us(400, reset_delay_complete, NULL, true);
    }
}

static void dma_setup_init(PIO pio, uint sm) {
    dma_claim_mask(DMA_CHANNELS_MASK);

    dma_channel_config channel_config = dma_channel_get_default_config(DMA_CHANNEL);
    channel_config_set_transfer_data_size(&channel_config, DMA_SIZE_32);
    channel_config_set_dreq(&channel_config, pio_get_dreq(pio, sm, true));
    channel_config_set_irq_quiet(&channel_config, false);
    dma_channel_configure(DMA_CHANNEL, &channel_config, &pio->txf[sm], led_data, NUM_PIXELS, false);

    irq_set_exclusive_handler(DMA_IRQ_0, dma_complete_handler);
    dma_channel_set_irq0_enabled(DMA_CHANNEL, true);
    irq_set_enabled(DMA_IRQ_0, true);
}

void ws2812_dma_init(uint pin) {
    // Initialize PIO for WS2812 single chain
    // We assume the program is ws2812_program from ws2812.pio.h
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &ws2812_pio, &ws2812_sm, &ws2812_offset, pin, 1, true);
    hard_assert(success);

    ws2812_program_init(ws2812_pio, ws2812_sm, ws2812_offset, pin, 800000, false);
    
    // Initialize DMA and semaphore
    sem_init(&reset_delay_complete_sem, 1, 1);
    dma_setup_init(ws2812_pio, ws2812_sm);
}

void ws2812_put_pixel(uint32_t pixel_grb) {
    led_data[led_index++] = pixel_grb << 8u;
}

void ws2812_put_pixels(uint32_t *pixels) {
    for (uint i = 0; i < NUM_PIXELS; i++) {
        led_data[i] = pixels[i] << 8u;
    }
}

void ws2812_reset_pixel_index(void) {
    led_index = 0;
}

void ws2812_dma_trigger(void) {
    dma_channel_set_read_addr(DMA_CHANNEL, led_data, true);
}

bool ws2812_dma_ready(void) {
    return sem_try_acquire(&reset_delay_complete_sem);
}

void ws2812_dma_handle(void) {
    if (ws2812_dma_ready()) {
        ws2812_dma_trigger();
    }
}