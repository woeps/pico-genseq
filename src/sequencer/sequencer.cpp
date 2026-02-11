#include "sequencer.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include <algorithm>
#include <random>
#include "pitch_set.h"
#include "velocity_set.h"
#include "gate_set.h"
#include "pattern.h"

namespace sequencer {

    // Global variables for multicore communication
    static Sequencer* globalSequencer = nullptr;

    // Multicore FIFO for command passing
    static void sequencer_task(uart_inst_t* uart);

    // Sequencer implementation
    Sequencer::Sequencer(uart_inst_t* uart, uint txPin, uint rxPin) :
        uart(uart),
        bpm(120),
        playing(false),
        lastTickTime(get_absolute_time()),
        midiClockEnabled(true),
        patterns({ Pattern() }) {
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
            for (auto& pattern : patterns) {
                if (!pattern.isActive()) continue;

                // Get non-const references to the pattern components
                GateSet& gateSet = const_cast<GateSet&>(pattern.getGateSet());
                PitchSet& pitchSet = const_cast<PitchSet&>(pattern.getPitchSet());
                VelocitySet& velocitySet = const_cast<VelocitySet&>(pattern.getVelocitySet());

                if (pitchSet.getPitches().empty() || velocitySet.getVelocities().empty() || gateSet.getGates().empty()) continue;

                Flank flank = gateSet.getFlank();

                if (flank == RISING) {
                    sendMidiNoteOn(pattern.getMidiChannel(), pitchSet.getPitch(), velocitySet.getVelocity());
                }
                else if (flank == FALLING) {
                    sendMidiNoteOff(pattern.getMidiChannel(), pitchSet.getPitch());
                    pitchSet.setPosition(pitchSet.getPosition() + 1);
                    velocitySet.setPosition(velocitySet.getPosition() + 1);
                }
                int nextGatePosition = gateSet.getPosition() + 1;
                gateSet.setPosition(nextGatePosition);
            }
        }
    }

    void Sequencer::processCommand(commands::CommandMessage msg) {
        switch (msg.cmd) {
        case commands::Command::PLAY:
            play();
            break;
        case commands::Command::STOP:
            stop();
            break;
        case commands::Command::BPM_SET:
            setBPM(msg.param1);
            break;
        case commands::Command::PATTERN_ACTIVATE:
            activatePattern(msg.param1);
            break;
        case commands::Command::PATTERN_DEACTIVATE:
            deactivatePattern(msg.param1);
            break;
            // Add more command handlers as needed
        case commands::Command::PATTERN_EUCLIDEAN_SET_LENGTH:
            patternSetEuclideanLength(msg.param1, msg.param2);
        }
    }

    void Sequencer::play() {
        // Send MIDI Start message if MIDI clock is enabled
        if (midiClockEnabled) {
            sendMidiByte(midi::SystemRealTimeMessage::START);
        }
        
        // Set playing state and initialize timing
        playing = true;
        lastTickTime = get_absolute_time();
        
        // Clear pattern notes tracking to start fresh
        patternNotes.clear();
    }

    void Sequencer::stop() {
        playing = false;

        // Send note off only for active notes
        for (uint8_t channel = 0; channel < 16; channel++) {
            for (uint8_t note = 0; note < 128; note++) {
                if (activeNotes[channel][note]) {
                    sendMidiNoteOff(channel, note);
                }
            }
        }

        // set all sets in all paterns to position 0
        for (auto& pattern : patterns) {
            pattern.getGateSet().reset();
            pattern.getPitchSet().reset();
            pattern.getVelocitySet().reset();
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

    void Sequencer::patternSetEuclideanLength(size_t patternIndex, size_t length) {
        printf("TODO: patternSetEuclideanLength: %d, %d\n", patternIndex, length);
        // if (patternIndex < patterns.size()) {
        //     patterns[patternIndex].setEuclideanLength(length);
        // }
    }

    void Sequencer::sendMidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        // Ensure channel and note are within valid ranges
        channel = channel & 0x0F;  // Limit to 0-15
        note = note & 0x7F;       // Limit to 0-127
        
        // Track this note as active
        activeNotes[channel][note] = true;
        
        // MIDI Note On: status byte + channel, note, velocity
        // MIDI channels are 1-based in the API but 0-based in the protocol
        uint8_t channelIndex = (channel > 0) ? (channel - 1) : 0;
        sendMidiByte(midi::ChannelVoiceMessage::NOTE_ON | (channelIndex & 0x0F));
        sendMidiByte(note & 0x7F);
        sendMidiByte(velocity & 0x7F);
    }

    void Sequencer::sendMidiNoteOff(uint8_t channel, uint8_t note) {
        // Ensure channel and note are within valid ranges
        channel = channel & 0x0F;  // Limit to 0-15
        note = note & 0x7F;       // Limit to 0-127
        
        // Mark this note as inactive
        activeNotes[channel][note] = false;
        
        // MIDI Note Off: status byte + channel, note, velocity (0)
        // MIDI channels are 1-based in the API but 0-based in the protocol
        uint8_t channelIndex = (channel > 0) ? (channel - 1) : 0;
        sendMidiByte(midi::ChannelVoiceMessage::NOTE_OFF | (channelIndex & 0x0F));
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
                commands::CommandMessage msg = commands::receiveCommand();
                globalSequencer->processCommand(msg);

                // Update the sequencer
                globalSequencer->update();
            }
        }
    }

    void createSequencerTask(uart_inst_t* uart, uint txPin, uint rxPin) {
        // Create a global sequencer instance
        static Sequencer sequencer(uart, txPin, rxPin);
        globalSequencer = &sequencer;
        globalSequencer->init();

        // Launch the sequencer task on the second core
        multicore_launch_core1(sequencer_task);
    }

} // namespace sequencer
