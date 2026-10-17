#include "sequencer.h"
#include "midi_messages.h"
#include "../common/pitch_set.h"
#include "../common/velocity_set.h"
#include "../common/gate_set.h"
#include "../common/const.h"
#include "../common/pattern.h"
#include "../instrumentation/PerformanceTrace.h"

namespace sequencer {

Sequencer::Sequencer(IMidiOutput& output, IMonotonicClock& clock)
    : output(output),
      clock(clock),
      patterns({common::Pattern()}),
      bpm(120),
      playing(false),
      lastTickTime(clock.nowMicros()),
      midiClockEnabled(true) {
    // MIDI transport and monotonic time are injected; no hardware setup here.
}

void Sequencer::init() {
    // TODO: reset values to default
}

void Sequencer::update() {
    if (!playing) return;

    // Calculate time for one tick based on BPM.
    const uint64_t tickDurationUs = 60ULL * 1000ULL * 1000ULL / (bpm * PPQN);

    // Advance from the ideal prior deadline so ordinary polling jitter does not
    // shift the phase. Milestone 2's later Scheduler work will replace this
    // one-tick service behavior with bounded reconciliation and fault handling.
    const uint64_t currentTime = clock.nowMicros();
    const uint64_t dueTime = lastTickTime + tickDurationUs;
    if (currentTime >= dueTime) {
        const uint64_t latenessUs = currentTime - dueTime;
        const uint64_t missedTickCount = latenessUs / tickDurationUs;
        metrics.observeSchedulerTick(latenessUs, missedTickCount);
        lastTickTime = dueTime;

        instrumentation::PerformanceTrace::tickBegin();

        // Process all active patterns.
        for (auto& pattern : patterns) {
            if (!pattern.isActive()) continue;

            common::GateSet& gateSet = pattern.getGateSet();
            common::PitchSet& pitchSet = pattern.getPitchSet();
            common::VelocitySet& velocitySet = pattern.getVelocitySet();

            if (pitchSet.getPitches().empty() || velocitySet.getVelocities().empty() ||
                gateSet.getGates().empty()) {
                continue;
            }

            const common::Flank flank = gateSet.getFlank();

            if (flank == common::RISING) {
                sendMidiNoteOn(pattern.getCanonicalChannel(), pitchSet.getPitch(),
                               velocitySet.getVelocity());
            } else if (flank == common::FALLING) {
                sendMidiNoteOff(pattern.getCanonicalChannel(), pitchSet.getPitch());
                pitchSet.advance();
                velocitySet.advance();
            }
            const uint32_t nextGatePosition = static_cast<uint32_t>(
                (gateSet.getPosition() + 1) % gateSet.getGates().size());
            gateSet.setPosition(nextGatePosition);
        }

        instrumentation::PerformanceTrace::tickEnd();
    }
}

void Sequencer::processCommand(commands::CommandMessage msg) {
    if (!canApplyCommand(msg)) return;

    instrumentation::PerformanceTrace::operationApplyBegin();

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
        case commands::Command::PATTERN_GATE_SET:
            setPatternGateSet(msg.param1, msg.gates);
            break;
        case commands::Command::PATTERN_PITCH_SET:
            setPatternPitchSet(msg.param1, msg.pitchCount, msg.pitchOrder, msg.pitches);
            break;
        case commands::Command::PATTERN_VELOCITY_SET:
            setPatternVelocitySet(msg.param1, msg.velocityCount, msg.velocityOrder,
                                  msg.velocities);
            break;
        case commands::Command::PATTERN_ADD:
            addPattern(common::Pattern());
            break;
        case commands::Command::PATTERN_REMOVE:
            removePattern(msg.param1);
            break;
        default:
            break;
    }

    ++metrics.operationsApplied;
    instrumentation::PerformanceTrace::operationApplyEnd();
}

bool Sequencer::canApplyCommand(const commands::CommandMessage& msg) const {
    switch (msg.cmd) {
        case commands::Command::PLAY:
        case commands::Command::STOP:
        case commands::Command::BPM_SET:
        case commands::Command::PATTERN_ADD:
            return true;
        case commands::Command::PATTERN_ACTIVATE:
        case commands::Command::PATTERN_DEACTIVATE:
        case commands::Command::PATTERN_GATE_SET:
            return msg.param1 < patterns.size();
        case commands::Command::PATTERN_PITCH_SET:
            return msg.param1 < patterns.size() && msg.pitchCount <= msg.pitches.size();
        case commands::Command::PATTERN_VELOCITY_SET:
            return msg.param1 < patterns.size() &&
                   msg.velocityCount <= msg.velocities.size();
        case commands::Command::PATTERN_REMOVE:
            return patterns.size() > 1 && msg.param1 < patterns.size();
        case commands::Command::NOOP:
        default:
            return false;
    }
}

void Sequencer::resetPerformanceMetrics() {
    metrics = PerformanceMetrics{};
}

void Sequencer::play() {
    // Send MIDI Start message if MIDI clock is enabled.
    if (midiClockEnabled) {
        sendMidiByte(midi::SystemRealTimeMessage::START);
    }

    // Set playing state and initialize timing.
    playing = true;
    lastTickTime = clock.nowMicros();

    // Clear pattern notes tracking to start fresh.
    patternNotes.clear();
}

void Sequencer::stop() {
    playing = false;

    // Send note off only for active notes. activeNotes is indexed by canonical
    // protocol channel (0..15), so release through the same canonical channel.
    for (uint8_t protocolChannel = 0; protocolChannel < 16; protocolChannel++) {
        for (uint8_t note = 0; note < 128; note++) {
            if (activeNotes[protocolChannel][note]) {
                sendMidiNoteOff(common::MidiChannel(protocolChannel), note);
            }
        }
    }

    // Set all sets in all patterns to position 0.
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

void Sequencer::setPatternGateSet(size_t patternIndex,
                                  const std::vector<bool>& gates) {
    if (patternIndex < patterns.size()) {
        patterns[patternIndex].setGateSet(common::GateSet(gates));
    }
}

void Sequencer::setPatternPitchSet(size_t patternIndex, uint8_t count,
                                   common::PlayingOrder order,
                                   const std::vector<uint8_t>& pitches) {
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

void Sequencer::sendMidiNoteOn(common::MidiChannel channel, uint8_t note, uint8_t velocity) {
    // Ensure note is within range; the channel is already canonical 0..15.
    note = note & 0x7F;

    // Track this note as active, keyed by canonical protocol channel.
    activeNotes[channel.value][note] = true;

    sendMidiByte(midi::ChannelVoiceMessage::NOTE_ON | common::midi_channel::toStatusNibble(channel));
    sendMidiByte(note & 0x7F);
    sendMidiByte(velocity & 0x7F);
}

void Sequencer::sendMidiNoteOff(common::MidiChannel channel, uint8_t note) {
    // Ensure note is within range; the channel is already canonical 0..15.
    note = note & 0x7F;

    // Mark this note as inactive, keyed by canonical protocol channel.
    activeNotes[channel.value][note] = false;

    sendMidiByte(midi::ChannelVoiceMessage::NOTE_OFF | common::midi_channel::toStatusNibble(channel));
    sendMidiByte(note & 0x7F);
    sendMidiByte(0);
}

void Sequencer::sendMidiByte(uint8_t byte) {
    instrumentation::PerformanceTrace::midiDispatchBegin();
    output.write(byte);
    instrumentation::PerformanceTrace::midiDispatchEnd();
    ++metrics.midiBytesDispatched;
}

}  // namespace sequencer
