#include "sequencer.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include <algorithm>
#include <random>
#include "pitch_set.h"
#include "velocity_set.h"
#include "rhythm_set.h"
#include "pattern.h"

namespace sequencer {

// Global variables for multicore communication
static Sequencer* globalSequencer = nullptr;

// Multicore FIFO for command passing
static void sequencer_task(uart_inst_t *uart);

// Sequencer implementation
Sequencer::Sequencer(uart_inst_t *uart, uint txPin, uint rxPin) : 
    uart(uart),
    currentTick(0),
    bpm(120),
    playing(false),
    lastTickTime(get_absolute_time()),
    patterns({Pattern()}) {
    // Initialize UART for MIDI
    uart_init(uart, MIDI_BAUD_RATE);
    
    // Configure UART pins (assuming UART1 uses GPIO 4 and 5)
    gpio_set_function(txPin, GPIO_FUNC_UART);
    gpio_set_function(rxPin, GPIO_FUNC_UART);
    }

void Sequencer::init() {
    // TODO: reset values to default
}

void Sequencer::update() {
    if (!playing) return;
    
    // Calculate time for one tick based on BPM
    uint32_t tickDurationUs = 60 * 1000 * 1000 / (bpm * PPQN);
    
    // Check if it's time for the next tick
    absolute_time_t currentTime = get_absolute_time();
    if (absolute_time_diff_us(lastTickTime, currentTime) >= tickDurationUs) {
        lastTickTime = currentTime;
        
        // Process all active patterns
        for (const auto& pattern : patterns) {
            if (!pattern.isActive()) continue;
            
            const auto& rhythmSteps = pattern.getRhythmSet().getSteps();
            const auto& pitches = pattern.getPitchSet().getPitches();
            const auto& velocities = pattern.getVelocitySet().getVelocities();
            
            if (pitches.empty() || velocities.empty() || rhythmSteps.empty()) continue;
            
            // Calculate pattern length in ticks
            uint32_t patternLength = 0;
            for (const auto& step : rhythmSteps) {
                patternLength = std::max(patternLength, step.tick + step.duration);
            }
            
            if (patternLength == 0) continue;
            
            // Calculate current position within the pattern
            uint32_t patternPosition = currentTick % patternLength;
            
            // Check if any notes should be triggered at this tick
            for (size_t i = 0; i < rhythmSteps.size(); i++) {
                const auto& step = rhythmSteps[i];
                
                // Note on
                if (patternPosition == step.tick) {
                    // Determine which pitch and velocity to play based on the play mode
                    size_t pitchIndex = 0;
                    size_t velocityIndex = 0;
                    
                    switch (pattern.getPlayMode()) {
                        case PlayMode::FORWARD:
                            pitchIndex = i % pitches.size();
                            velocityIndex = i % velocities.size();
                            break;
                        case PlayMode::BACKWARD:
                            pitchIndex = (pitches.size() - 1 - (i % pitches.size())) % pitches.size();
                            velocityIndex = (velocities.size() - 1 - (i % velocities.size())) % velocities.size();
                            break;
                        case PlayMode::PENDULUM: {
                            size_t pitchCycle = pitches.size() * 2 - 2;
                            if (pitchCycle == 0) pitchCycle = 1; // Handle single pitch case
                            size_t pitchPos = i % pitchCycle;
                            pitchIndex = pitchPos < pitches.size() ? pitchPos : (pitchCycle - pitchPos);
                            
                            size_t velocityCycle = velocities.size() * 2 - 2;
                            if (velocityCycle == 0) velocityCycle = 1; // Handle single velocity case
                            size_t velocityPos = i % velocityCycle;
                            velocityIndex = velocityPos < velocities.size() ? velocityPos : (velocityCycle - velocityPos);
                            break;
                        }
                        case PlayMode::RANDOM: {
                            static std::random_device rd;
                            static std::mt19937 gen(rd());
                            std::uniform_int_distribution<> pitchDistrib(0, pitches.size() - 1);
                            std::uniform_int_distribution<> velocityDistrib(0, velocities.size() - 1);
                            pitchIndex = pitchDistrib(gen);
                            velocityIndex = velocityDistrib(gen);
                            break;
                        }
                    }
                    
                    uint8_t pitch = pitches[pitchIndex];
                    uint8_t velocity = velocities[velocityIndex];
                    sendMidiNoteOn(pattern.getMidiChannel(), pitch, velocity);
                }
                
                // Note off
                if (patternPosition == (step.tick + step.duration) % patternLength) {
                    // Find the pitch that was turned on at step.tick
                    size_t pitchIndex = 0;
                    switch (pattern.getPlayMode()) {
                        case PlayMode::FORWARD:
                            pitchIndex = i % pitches.size();
                            break;
                        case PlayMode::BACKWARD:
                            pitchIndex = (pitches.size() - 1 - (i % pitches.size())) % pitches.size();
                            break;
                        case PlayMode::PENDULUM: {
                            size_t cycle = pitches.size() * 2 - 2;
                            if (cycle == 0) cycle = 1; // Handle single pitch case
                            size_t pos = i % cycle;
                            pitchIndex = pos < pitches.size() ? pos : (cycle - pos);
                            break;
                        }
                        case PlayMode::RANDOM: {
                            // For random mode, we don't know which note was played
                            // So we'll just turn off all notes (handled in the stop() method)
                            break;
                        }
                    }
                    
                    if (pattern.getPlayMode() != PlayMode::RANDOM) {
                        uint8_t pitch = pitches[pitchIndex];
                        sendMidiNoteOff(pattern.getMidiChannel(), pitch);
                    }
                }
            }
        }
        
        currentTick++;
    }
}

void Sequencer::processCommand(const commands::CommandMessage& msg) {
    switch (msg.cmd) {
        case commands::Command::PLAY:
            play();
            break;
        case commands::Command::STOP:
            stop();
            break;
        case commands::Command::SET_BPM:
            setBPM(msg.param1);
            break;
        case commands::Command::ACTIVATE_PATTERN:
            activatePattern(msg.param1);
            break;
        case commands::Command::DEACTIVATE_PATTERN:
            deactivatePattern(msg.param1);
            break;
        // Add more command handlers as needed
    }
}

void Sequencer::receiveCommand() {
    if (multicore_fifo_rvalid()) {
        uint32_t raw_cmd = multicore_fifo_pop_blocking();
        commands::CommandMessage msg;
        msg.cmd = static_cast<commands::Command>(raw_cmd & 0xFF);
        msg.param1 = (raw_cmd >> 8) & 0xFFFF;
        msg.param2 = (raw_cmd >> 24) & 0xFF;
        
        processCommand(msg);
    }
}

void Sequencer::play() {
    currentTick = 0;
    playing = true;
    lastTickTime = get_absolute_time();
}

void Sequencer::stop() {
    playing = false;
    
    // Send note off for all MIDI channels and notes
    for (uint8_t channel = 0; channel < 16; channel++) {
        for (uint8_t note = 0; note < 128; note++) {
            sendMidiNoteOff(channel, note);
        }
    }
}

void Sequencer::setBPM(uint16_t bpm) {
    this->bpm = bpm;
}

void Sequencer::addPattern(const Pattern& pattern) {
    patterns.push_back(pattern);
}

void Sequencer::activatePattern(size_t index) {
    if (index < patterns.size()) {
        patterns[index].setActive(true);
    }
}

void Sequencer::deactivatePattern(size_t index) {
    if (index < patterns.size()) {
        patterns[index].setActive(false);
    }
}

void Sequencer::sendMidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    // MIDI Note On: 0x9n (n = channel), note, velocity
    sendMidiByte(0x90 | (channel & 0x0F));
    sendMidiByte(note & 0x7F);
    sendMidiByte(velocity & 0x7F);
}

void Sequencer::sendMidiNoteOff(uint8_t channel, uint8_t note) {
    // MIDI Note Off: 0x8n (n = channel), note, velocity (0)
    sendMidiByte(0x80 | (channel & 0x0F));
    sendMidiByte(note & 0x7F);
    sendMidiByte(0); // velocity 0
}

void Sequencer::sendMidiByte(uint8_t byte) {
    uart_putc_raw(uart, byte);
}

// Sequencer task for second core
static void sequencer_task() {
    // Make sure the global sequencer is initialized
    if (globalSequencer) {
        while (true) {
            // Check for commands from the UI core
            globalSequencer->receiveCommand();
            
            // Update the sequencer
            globalSequencer->update();
            
            // Small delay to prevent tight looping
            sleep_us(100);
        }
    }
}

void createSequencerTask(uart_inst_t *uart, uint txPin, uint rxPin) {
    // Create a global sequencer instance
    static Sequencer sequencer(uart, txPin, rxPin);
    globalSequencer = &sequencer;
    globalSequencer->init();
    
    // Launch the sequencer task on the second core
    multicore_launch_core1(sequencer_task);
}

} // namespace sequencer
