#include "sequencer.h"
#include "midi_messages.h"
#include "../common/pitch_set.h"
#include "../common/velocity_set.h"
#include "../common/gate_set.h"
#include "../common/const.h"
#include "../common/pattern.h"
#include <cstdio>

namespace sequencer {

    // Sequencer implementation
    Sequencer::Sequencer(IMidiOutput& output) :
        output(output),
        bpm(120),
        playing(false),
        lastTickTime(get_absolute_time()),
        midiClockEnabled(true),
        patterns({ common::Pattern() }) {
        // MIDI transport is injected; no UART/pin setup here anymore.
    }

    void Sequencer::init() {
        // TODO: reset values to default
    }

    void Sequencer::update() {
        if (!playing) return;

        // Calculate time for one tick based on BPM
        uint32_t tickDurationUs = 60 * 1000 * 1000 / (bpm * PPQN);

        // Check if it's time for the next tick.
        // Advance lastTickTime by exactly tickDurationUs (not by setting it to currentTime)
        // so accumulated late-detection jitter doesn't shift the phase of future ticks.
        absolute_time_t currentTime = get_absolute_time();
        if (absolute_time_diff_us(lastTickTime, currentTime) >= tickDurationUs) {
            lastTickTime = delayed_by_us(lastTickTime, tickDurationUs);

            // Process all active patterns
            for (auto& pattern : patterns) {
                if (!pattern.isActive()) continue;

                // Get non-const references to the pattern components
                common::GateSet& gateSet = const_cast<common::GateSet&>(pattern.getGateSet());
                common::PitchSet& pitchSet = const_cast<common::PitchSet&>(pattern.getPitchSet());
                common::VelocitySet& velocitySet = const_cast<common::VelocitySet&>(pattern.getVelocitySet());

                if (pitchSet.getPitches().empty() || velocitySet.getVelocities().empty() || gateSet.getGates().empty()) continue;

                common::Flank flank = gateSet.getFlank();

                if (flank == common::RISING) {
                    sendMidiNoteOn(pattern.getMidiChannel(), pitchSet.getPitch(), velocitySet.getVelocity());
                }
                else if (flank == common::FALLING) {
                    sendMidiNoteOff(pattern.getMidiChannel(), pitchSet.getPitch());
                    pitchSet.advance();
                    velocitySet.advance();
                }
                const uint32_t nextGatePosition = static_cast<uint32_t>(
                    (gateSet.getPosition() + 1) % gateSet.getGates().size());
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
        case commands::Command::PATTERN_GATE_SET:
            setPatternGateSet(msg.param1, msg.gates);
            break;
        case commands::Command::PATTERN_PITCH_SET:
            setPatternPitchSet(msg.param1, msg.pitchCount, msg.pitchOrder, msg.pitches);
            break;
        case commands::Command::PATTERN_VELOCITY_SET:
            setPatternVelocitySet(msg.param1, msg.velocityCount, msg.velocityOrder, msg.velocities);
            break;
        case commands::Command::PATTERN_ADD:
            addPattern(common::Pattern());
            break;
        case commands::Command::PATTERN_REMOVE:
            removePattern(msg.param1);
            break;
        default: break;
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

    void Sequencer::addPattern(const common::Pattern& pattern) {
        patterns.push_back(pattern);
    }

    void Sequencer::removePattern(size_t index) {
        if (patterns.size() <= 1 || index >= patterns.size()) return;
        patterns.erase(patterns.begin() + index);
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

    void Sequencer::setPatternGateSet(size_t patternIndex, const std::vector<bool>& gates) {
        if (patternIndex < patterns.size()) {
            patterns[patternIndex].setGateSet(common::GateSet(gates));
        }
    }

    void Sequencer::setPatternPitchSet(size_t patternIndex, uint8_t count,
                                       common::PlayingOrder order, const std::vector<uint8_t>& pitches) {
        if (patternIndex >= patterns.size()) return;
        std::vector<uint8_t> activePitches(pitches.begin(), pitches.begin() + count);
        common::PitchSet pitchSet(activePitches, order);
        patterns[patternIndex].setPitchSet(pitchSet);
    }

    void Sequencer::setPatternVelocitySet(size_t patternIndex, uint8_t count,
                                          common::PlayingOrder order,
                                          const std::vector<uint8_t>& velocities) {
        if (patternIndex >= patterns.size()) return;
        std::vector<uint8_t> activeVelocities(velocities.begin(), velocities.begin() + count);
        common::VelocitySet velocitySet(activeVelocities, order);
        patterns[patternIndex].setVelocitySet(velocitySet);
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
        output.write(byte);
    }

} // namespace sequencer
