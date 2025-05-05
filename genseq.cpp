#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "sequencer/sequencer.h"
#include "ui/ui.h"

// PIN ASSIGNMENTS & CONFIG

// UI
#define PIN_PLAY_STOP_BUTTON 20

#define PIN_ENCODER_A 18
#define PIN_ENCODER_B 19

// DISPLAY
#define DISPLAY_I2C i2c0
#define PIN_DISPLAY_SDA 4
#define PIN_DISPLAY_SCL 5
#define DISPLAY_I2C_ADDR 0x27

#define PIN_PLAY_LED 25

// UART MIDI
#define MIDI_UART uart1  // Using UART1 for MIDI
#define PIN_MIDI_UART_TX 8 // MIDI_UART_TX
#define PIN_MIDI_UART_RX 9 // MIDI_UART_RX



int main() {
    // Initialize stdio for debugging
    stdio_init_all();

    printf("GenSeq MIDI Sequencer starting...\n");
    
    // Initialize the sequencer on core 1
    multicore_reset_core1();
    
    // Start the sequencer task on core 1
    sequencer::createSequencerTask(MIDI_UART, PIN_MIDI_UART_TX, PIN_MIDI_UART_RX);
    printf("Sequencer task started on core0.\n");
    
    // Wait for core 1 to start
    sleep_ms(100);
    
    // Start the UI task on core 0
    ui::createUITask(
        PIN_PLAY_STOP_BUTTON,
        PIN_ENCODER_A,
        PIN_ENCODER_B,
        DISPLAY_I2C,
        DISPLAY_I2C_ADDR,
        PIN_DISPLAY_SDA,
        PIN_DISPLAY_SCL,
        PIN_PLAY_LED
    );
    
    // This should never be reached
    printf("GenSeq MIDI Sequencer ended.\n");
    return 0;
}
