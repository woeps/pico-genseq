#include "pattern.h"
#include "const.h"

namespace common {

    // Pattern implementation
    Pattern::Pattern(const PitchSet& pitchSet, const VelocitySet& velocitySet, const GateSet& gateSet, int midiChannel) :
        pitchSet(pitchSet),
        velocitySet(velocitySet),
        gateSet(gateSet),
        midiChannel(midiChannel),
        active(false)
    {
    }

    Pattern::Pattern() :
        pitchSet({
            60, // C4
            // 62, // D4
            // 64, // E4
            // 65, // F4
            67, // G4
            69, // A4
            // 71, // B4
            72  // C5
            }),
            active(true),
            velocitySet({ 100, 20 }),
            gateSet(GateSet::createEuclidean(12, 2, 0, PPQN)),
            // gateSet(GateSet({ true, false, false, false, false, false, false, false,
            //                   false, false, false, false, false, false, false, false,
            //                   false, false, false, false, false, false, false, false
            //                 })),
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
        // GateSet gateSet = GateSet::createEuclidean(8, 5, 0, PPQN * 4);

        // Create a pattern with the pitch set, velocity set, and rhythm set
        //Pattern pattern(pitchSet, velocitySet, rhythmSet);
        //pattern.setActive(true);

        //return pattern;
    }

    PitchSet& Pattern::getPitchSet() {
        return pitchSet;
    }

    void Pattern::setPitchSet(const PitchSet& pitchSet) {
        this->pitchSet = pitchSet;
    }

    VelocitySet& Pattern::getVelocitySet() {
        return velocitySet;
    }

    void Pattern::setVelocitySet(const VelocitySet& velocitySet) {
        this->velocitySet = velocitySet;
    }

    GateSet& Pattern::getGateSet() {
        return gateSet;
    }

    void Pattern::setGateSet(const GateSet& gateSet) {
        this->gateSet = gateSet;
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

} // namespace common
