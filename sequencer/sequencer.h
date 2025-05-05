#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "../commands/command.h"
#include "pitch_set.h"
#include "velocity_set.h"
#include "rhythm_set.h"
#include "play_mode.h"
#include "pattern.h"

namespace sequencer {

// Constants
#define MIDI_BAUD_RATE 31250  // Standard MIDI baud rate

// Main sequencer class
class Sequencer {
public:
    Sequencer(uart_inst_t *uart, uint tx, uint rx);

    void init();
    void update();
    void receiveCommand();
    
private:
    uart_inst_t *uart;
    std::vector<Pattern> patterns;
    uint32_t currentTick;
    uint16_t bpm;
    bool playing;
    absolute_time_t lastTickTime;
    
    void processCommand(const commands::CommandMessage& msg);
    void play();
    void stop();
    void setBPM(uint16_t bpm);
    
    void addPattern(const Pattern& pattern);
    void activatePattern(size_t index);
    void deactivatePattern(size_t index);
    
    void sendMidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void sendMidiNoteOff(uint8_t channel, uint8_t note);
    void sendMidiByte(uint8_t byte);

};

// Function to create the sequencer task for the second core
void createSequencerTask(uart_inst_t *uart, uint txPin, uint rxPin);

} // namespace sequencer
