#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "sequencer/sequencer.h"
#include "ui/ui.h"

// PIN ASSIGNMENTS & CONFIG

// BUTTON
#define BUTTON_E_PIN 20
#define BUTTON_A_PIN 21

// ENCODER
#define ENCODER_PIN_A 14
#define ENCODER_PIN_B 15
#define ENCODER_PIO pio0
#define ENCODER_SM 0

// DISPLAY
#define DISPLAY_I2C i2c0
#define DISPLAY_PIN_SDA 4
#define DISPLAY_PIN_SCL 5
#define DISPLAY_I2C_ADDR 0x27

#define LED_PIN 25

// UART MIDI
#define MIDI_UART uart1    // Using UART1 for MIDI
#define MIDI_UART_PIN_TX 8 // MIDI_UART_TX
#define MIDI_UART_PIN_RX 9 // MIDI_UART_RX

int main()
{
    // Initialize stdio for debugging
    stdio_init_all();

    printf("GenSeq MIDI Sequencer starting...\n");

    // Initialize the sequencer on core 1
    multicore_reset_core1();

    // Start the sequencer task on core 1
    sequencer::createSequencerTask(MIDI_UART, MIDI_UART_PIN_TX, MIDI_UART_PIN_RX);
    printf("Sequencer task started on core0.\n");

    // Wait for core 1 to start
    sleep_ms(100);

    // Start the UI task on core 0
    ui::createUITask(
        BUTTON_E_PIN,
        BUTTON_A_PIN,
        ENCODER_PIN_A,
        ENCODER_PIN_B,
        ENCODER_PIO,
        ENCODER_SM,
        DISPLAY_I2C,
        DISPLAY_I2C_ADDR,
        DISPLAY_PIN_SDA,
        DISPLAY_PIN_SCL,
        LED_PIN);

    // This should never be reached
    printf("GenSeq MIDI Sequencer ended.\n");
    return 0;
}
