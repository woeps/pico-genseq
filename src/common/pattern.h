#pragma once

#include "pitch_set.h"
#include "velocity_set.h"
#include "gate_set.h"
#include "GenerationParameters.h"

namespace common {

    // Class to represent a pattern (combination of pitch, velocity and rhythm)
    class Pattern {
    public:
        Pattern(const PitchSet& pitchSet, const VelocitySet& velocitySet, const GateSet& gateSet);
        Pattern();

        PitchSet& getPitchSet();
        void setPitchSet(const PitchSet& pitchSet);

        VelocitySet& getVelocitySet();
        void setVelocitySet(const VelocitySet& velocitySet);

        GateSet& getGateSet();
        void setGateSet(const GateSet& gateSet);

        bool isActive() const;
        void setActive(bool active);

        // Generation Parameters (generative-engine-expansion). Defaults to the
        // behavior-preserving value object, so an existing Pattern constructed
        // without them emits exactly as before (design invariant 1).
        GenerationParameters& getGenerationParameters();
        const GenerationParameters& getGenerationParameters() const;
        void setGenerationParameters(const GenerationParameters& params);

    private:
        PitchSet pitchSet;
        VelocitySet velocitySet;
        GateSet gateSet;
        bool active;
        GenerationParameters generationParameters;
    };

} // namespace common
