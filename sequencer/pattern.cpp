#include "pattern.h"

namespace sequencer {

// Pattern implementation
Pattern::Pattern(const PitchSet& pitchSet, const VelocitySet& velocitySet, const RhythmSet& rhythmSet, PlayMode playMode, int midiChannel)
    : pitchSet(pitchSet), velocitySet(velocitySet), rhythmSet(rhythmSet), playMode(playMode), midiChannel(midiChannel), active(false) {}

Pattern::Pattern(): pitchSet({
        60, // C4
        // 62, // D4
        // 64, // E4
        // 65, // F4
        // 67, // G4
        // 69, // A4
        // 71, // B4
        // 72  // C5
    }),
    active(true),
    velocitySet({100, 40}),
    rhythmSet(RhythmSet::createEuclidean(4, 1, 0, PPQN)),
    midiChannel(1) {
    // Create a C major scale
    // std::vector<uint8_t> cMajorScale = {
    //     60, // C4
    //     62, // D4
    //     64, // E4
    //     65, // F4
    //     67, // G4
    //     69, // A4
    //     71, // B4
    //     72  // C5
    // };
    // PitchSet pitchSet(cMajorScale);
    
    // Create a velocity set
    // std::vector<uint8_t> velocities = {100, 80, 100, 90, 110, 70, 90, 100};
    // VelocitySet velocitySet(velocities);
    
    // Create a simple euclidean rhythm (8 steps, 5 pulses)
    // RhythmSet rhythmSet = RhythmSet::createEuclidean(8, 5, 0, PPQN);
    
    // Create a pattern with the pitch set, velocity set, and rhythm set
    //Pattern pattern(pitchSet, velocitySet, rhythmSet, PlayMode::FORWARD);
    //pattern.setActive(true);
    
    //return pattern;
}

const PitchSet& Pattern::getPitchSet() const {
    return pitchSet;
}

void Pattern::setPitchSet(const PitchSet& pitchSet) {
    this->pitchSet = pitchSet;
}

const VelocitySet& Pattern::getVelocitySet() const {
    return velocitySet;
}

void Pattern::setVelocitySet(const VelocitySet& velocitySet) {
    this->velocitySet = velocitySet;
}

const RhythmSet& Pattern::getRhythmSet() const {
    return rhythmSet;
}

void Pattern::setRhythmSet(const RhythmSet& rhythmSet) {
    this->rhythmSet = rhythmSet;
}

PlayMode Pattern::getPlayMode() const {
    return playMode;
}

void Pattern::setPlayMode(PlayMode playMode) {
    this->playMode = playMode;
}

int Pattern::getMidiChannel() const {
    return midiChannel;
}

void Pattern::setMidiChannel(int midiChannel) {
    this->midiChannel = midiChannel;
}

bool Pattern::isActive() const {
    return active;
}

void Pattern::setActive(bool active) {
    this->active = active;
}

} // namespace sequencer
