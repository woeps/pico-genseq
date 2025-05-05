#pragma once

#include "pitch_set.h"
#include "velocity_set.h"
#include "rhythm_set.h"
#include "play_mode.h"

namespace sequencer {

// Class to represent a pattern (combination of pitch, velocity and rhythm)
class Pattern {
public:
    Pattern(const PitchSet& pitchSet, const VelocitySet& velocitySet, const RhythmSet& rhythmSet, PlayMode playMode = PlayMode::FORWARD, int midiChannel = 1);
    Pattern();
    
    const PitchSet& getPitchSet() const;
    void setPitchSet(const PitchSet& pitchSet);
    
    const VelocitySet& getVelocitySet() const;
    void setVelocitySet(const VelocitySet& velocitySet);
    
    const RhythmSet& getRhythmSet() const;
    void setRhythmSet(const RhythmSet& rhythmSet);
    
    PlayMode getPlayMode() const;
    void setPlayMode(PlayMode playMode);
    
    int getMidiChannel() const;
    void setMidiChannel(int midiChannel);
    
    bool isActive() const;
    void setActive(bool active);
    
private:
    PitchSet pitchSet;
    VelocitySet velocitySet;
    RhythmSet rhythmSet;
    PlayMode playMode;
    int midiChannel;
    bool active;
};

} // namespace sequencer
