#pragma once

#include "pitch_set.h"
#include "velocity_set.h"
#include "gate_set.h"

namespace common {

    // Class to represent a pattern (combination of pitch, velocity and rhythm)
    class Pattern {
    public:
        Pattern(const PitchSet& pitchSet, const VelocitySet& velocitySet, const GateSet& gateSet, int midiChannel = 1);
        Pattern();

        PitchSet& getPitchSet();
        void setPitchSet(const PitchSet& pitchSet);

        VelocitySet& getVelocitySet();
        void setVelocitySet(const VelocitySet& velocitySet);

        GateSet& getGateSet();
        void setGateSet(const GateSet& gateSet);

        int getMidiChannel() const;
        void setMidiChannel(int midiChannel);

        bool isActive() const;
        void setActive(bool active);

    private:
        PitchSet pitchSet;
        VelocitySet velocitySet;
        GateSet gateSet;
        int midiChannel;
        bool active;
    };

} // namespace common
