#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include <map>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "../commands/command.h"
#include "pitch_set.h"
#include "velocity_set.h"
#include "gate_set.h"
#include "pattern.h"
#include "midi_messages.h"

namespace sequencer {

    // Constants
#define MIDI_BAUD_RATE 31250  // Standard MIDI baud rate
#define PPQN 24

// Main sequencer class
    class Sequencer {
    public:
        Sequencer(uart_inst_t* uart, uint tx, uint rx);

        void init();
        void update();
        void receiveCommand();

    private:
        uart_inst_t* uart;
        std::vector<Pattern> patterns;
        uint16_t bpm;
        bool playing;
        absolute_time_t lastTickTime;
        bool midiClockEnabled;
        
        // Track active notes: activeNotes[channel][note] = true if the note is active
        bool activeNotes[16][128] = {{false}};
        
        // Track which notes are active for each pattern and position
        // patternNotes[patternIndex][position] = note number
        std::map<size_t, std::map<uint32_t, uint8_t>> patternNotes;

        void play();
        void stop();
        void setBPM(uint16_t bpm);
        void sendMidiClock();

        void addPattern(const Pattern& pattern);
        void activatePattern(size_t index);
        void deactivatePattern(size_t index);

        void sendMidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
        void sendMidiNoteOff(uint8_t channel, uint8_t note);
        void sendMidiByte(uint8_t byte);

    };

    // Function to create the sequencer task for the second core
    void createSequencerTask(uart_inst_t* uart, uint txPin, uint rxPin);

} // namespace sequencer
