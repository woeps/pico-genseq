#pragma once

#include <vector>
#include <cstdint>
#include <map>
#include "hardware/uart.h"
#include "../commands/command.h"
#include "../common/pattern.h"

namespace sequencer {

    // Constants
#define MIDI_BAUD_RATE 31250  // Standard MIDI baud rate

// Main sequencer class
    class Sequencer {
    public:
        Sequencer(uart_inst_t* uart, uint tx, uint rx);

        void init();
        void update();
        void processCommand(commands::CommandMessage msg);

    private:
        uart_inst_t* uart;
        std::vector<common::Pattern> patterns;
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

        void addPattern(const common::Pattern& pattern);
        void activatePattern(size_t index);
        void deactivatePattern(size_t index);
        void patternSetEuclideanLength(size_t patternIndex, size_t length);

        void sendMidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
        void sendMidiNoteOff(uint8_t channel, uint8_t note);
        void sendMidiByte(uint8_t byte);

    };

    // Function to create the sequencer task for the second core
    void createSequencerTask(uart_inst_t* uart, uint txPin, uint rxPin);

} // namespace sequencer
