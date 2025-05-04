#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "sequencer/sequencer.h"
#include "ui/ui.h"

// PIN ASSIGNMENTS

// UI
#define PIN_PLAY_STOP_BUTTON 20
#define PIN_ENCODER_A 18
#define PIN_ENCODER_B 19
#define PIN_DISPLAY_RS 6
#define PIN_DISPLAY_ENABLE 7
#define PIN_DISPLAY_D4 8
#define PIN_DISPLAY_D5 9
#define PIN_DISPLAY_D6 10
#define PIN_DISPLAY_D7 11
#define PIN_PLAY_LED 25

// MIDI
#define MIDI_UART uart1  // Using UART1 for MIDI



int main() {
    printf("GenSeq MIDI Sequencer starting...\n");

    // Initialize stdio for debugging
    stdio_init_all();
    
    // Initialize the sequencer on core 1
    multicore_reset_core1();
    
    // Start the sequencer task on core 1
    sequencer::createSequencerTask(MIDI_UART);
    
    // Wait for core 1 to start
    sleep_ms(100);
    
    // Start the UI task on core 0
    ui::createUITask(
        PIN_PLAY_STOP_BUTTON,
        PIN_ENCODER_A,
        PIN_ENCODER_B,
        PIN_DISPLAY_RS,
        PIN_DISPLAY_ENABLE,
        PIN_DISPLAY_D4,
        PIN_DISPLAY_D5,
        PIN_DISPLAY_D6,
        PIN_DISPLAY_D7,
        PIN_PLAY_LED
    );
    
    // This should never be reached
    printf("GenSeq MIDI Sequencer ended.\n");
    return 0;
}
