#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "IMidiOutput.h"
#include "IMonotonicClock.h"
#include "PerformanceMetrics.h"
#include "hardware/uart.h"
#include "../commands/command.h"
#include "../common/pattern.h"

namespace sequencer {

// Main sequencer class
class Sequencer {
public:
    Sequencer(IMidiOutput& output, IMonotonicClock& clock);

    void init();
    void update();
    void processCommand(commands::CommandMessage msg);

    const PerformanceMetrics& performanceMetrics() const { return metrics; }
    void resetPerformanceMetrics();

private:
    IMidiOutput& output;
    IMonotonicClock& clock;
    std::vector<common::Pattern> patterns;
    uint16_t bpm;
    bool playing;
    uint64_t lastTickTime;
    bool midiClockEnabled;
    PerformanceMetrics metrics{};

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
    void removePattern(size_t index);
    void activatePattern(size_t index);
    void deactivatePattern(size_t index);
    void setPatternGateSet(size_t patternIndex, const std::vector<bool>& gates);
    void setPatternPitchSet(size_t patternIndex, uint8_t count,
                            common::PlayingOrder order,
                            const std::vector<uint8_t>& pitches);
    void setPatternVelocitySet(size_t patternIndex, uint8_t count,
                               common::PlayingOrder order,
                               const std::vector<uint8_t>& velocities);

    void sendMidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void sendMidiNoteOff(uint8_t channel, uint8_t note);
    void sendMidiByte(uint8_t byte);
    bool canApplyCommand(const commands::CommandMessage& msg) const;
};

// Function to create the sequencer task for the second core
void createSequencerTask(uart_inst_t* uart, uint txPin, uint rxPin);

}  // namespace sequencer
