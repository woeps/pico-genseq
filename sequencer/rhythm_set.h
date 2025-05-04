#pragma once

#include <vector>
#include <cstdint>

namespace sequencer {

// Constants
constexpr uint32_t PPQN = 24;  // Pulses Per Quarter Note (standard MIDI resolution)

// Class to represent a rhythm (timing and gate information)
class RhythmSet {
public:
    struct Step {
        uint32_t tick;      // Timing in ticks
        uint32_t duration;  // Duration in ticks
    };

    RhythmSet(const std::vector<Step>& steps = {});
    const std::vector<Step>& getSteps() const;
    void setSteps(const std::vector<Step>& steps);
    
    // Generate euclidean rhythm
    static RhythmSet createEuclidean(uint8_t steps, uint8_t pulses, uint8_t rotation, uint32_t stepDuration);

private:
    std::vector<Step> steps;
};

} // namespace sequencer
