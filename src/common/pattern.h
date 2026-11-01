#pragma once

#include "pitch_set.h"
#include "velocity_set.h"
#include "gate_set.h"
#include "MidiChannel.h"
#include "GenerationParameters.h"

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

        // Canonical protocol channel (0..15) derived from the stored 1..16 user
        // channel. Use this on the MIDI emission / note-ownership path so the
        // channel is never aliased. See common/MidiChannel.h.
        MidiChannel getCanonicalChannel() const;

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
        int midiChannel;
        bool active;
        GenerationParameters generationParameters;
    };

} // namespace common
