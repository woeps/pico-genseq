#include "rhythm_set.h"
#include <algorithm>

namespace sequencer {

    // RhythmSet implementation
    RhythmSet::RhythmSet(const std::vector<Step>& steps) : steps(steps) {}

    const std::vector<RhythmSet::Step>& RhythmSet::getSteps() const {
        return steps;
    }

    void RhythmSet::setSteps(const std::vector<Step>& steps) {
        this->steps = steps;
    }

    RhythmSet RhythmSet::createEuclidean(uint8_t numSteps, uint8_t numPulses, uint8_t rotation, uint32_t stepDuration) {
        // Euclidean algorithm implementation (Bjorklund's algorithm)
        std::vector<bool> pattern(numSteps, false);

        if (numPulses >= numSteps) {
            // If pulses >= steps, all steps are active
            std::fill(pattern.begin(), pattern.end(), true);
        }
        else if (numPulses > 0) {
            // Calculate the spacing between pulses
            int bucket = 0;
            for (int i = 0; i < numSteps; i++) {
                bucket += numPulses;
                if (bucket >= numSteps) {
                    bucket -= numSteps;
                    pattern[(i + rotation) % numSteps] = true;
                }
            }
        }

        // Convert boolean pattern to RhythmSet::Step vector
        std::vector<RhythmSet::Step> steps;
        for (size_t i = 0; i < pattern.size(); i++) {
            if (pattern[i]) {
                RhythmSet::Step step;
                step.tick = i * stepDuration;
                step.duration = stepDuration / 2; // 50% gate time by default
                steps.push_back(step);
            }
        }

        return RhythmSet(steps);
    }

} // namespace sequencer
