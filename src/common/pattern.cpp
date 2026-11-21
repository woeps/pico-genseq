#include "pattern.h"
#include "const.h"

namespace common {

    // Pattern implementation
    Pattern::Pattern(const PitchSet& pitchSet, const VelocitySet& velocitySet, const GateSet& gateSet) :
        pitchSet(pitchSet),
        velocitySet(velocitySet),
        gateSet(gateSet),
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
            gateSet() {
            // gateSet(GateSet({ true, false, false, false, false, false, false, false,
            //                   false, false, false, false, false, false, false, false,
            //                   false, false, false, false, false, false, false, false
            //                 })),
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
        // GateSet gateSet = GateSet::createEuclidean(8, 5, 0, PPQN / 4, 50);

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



    bool Pattern::isActive() const {
        return active;
    }

    void Pattern::setActive(bool active) {
        this->active = active;
    }

    GenerationParameters& Pattern::getGenerationParameters() {
        return generationParameters;
    }

    const GenerationParameters& Pattern::getGenerationParameters() const {
        return generationParameters;
    }

    void Pattern::setGenerationParameters(const GenerationParameters& params) {
        this->generationParameters = params;
    }

} // namespace common
