#include <stdio.h>
#include "pico/stdio.h"
#include "pico/multicore.h"
#include "sequencer/sequencer.h"
#include "ui/ui.h"
#include "config/pins.h"

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
    ui::createUITask(LED_PIN, LED_MATRIX_PIN);

    // This should never be reached
    printf("GenSeq MIDI Sequencer ended.\n");
    return 0;
}
