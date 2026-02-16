#include "gate_set.h"
#include <algorithm>
#include <cstdio>
#include <map>

namespace common {

    std::map<Flank, char const*> flankStringMap = {
        { RISING,   "Rising" },
        { HIGH,     "High" },
        { FALLING,  "Falling" },
        { LOW,      "Low" },
    };
    char const* flankToString(Flank flank) {
        return flankStringMap[flank];
    }

    Flank getInitFlank(std::vector<bool> gates);

    // GateSet implementation
    GateSet::GateSet(const std::vector<bool>& gates) :
        gates(gates),
        position(0),
        previousPosition(0),
        flank(getInitFlank(gates))
    {
        printf("|");
        for (auto gate : gates) {
            printf("%s", gate ? "-" : "_");
        }
    }

    const std::vector<bool>& GateSet::getGates() const {
        return gates;
    }

    void GateSet::setGates(const std::vector<bool>& gates) {
        this->gates = gates;
    }

    uint32_t GateSet::getLength() const {
        return gates.size();
    }

    void GateSet::setPosition(uint8_t position) {
        if (position >= gates.size()) {
            printf("GateSet::setPosition: position %d is out of bounds for gate set of size %d\n", position, gates.size());
        }
        this->previousPosition = this->position;
        this->position = position % gates.size();

        bool previousGate = this->gates[this->previousPosition];
        bool currentGate = this->gates[this->position];

        if (!previousGate && currentGate) {
            this->flank = RISING;
        }
        else if (previousGate && !currentGate) {
            this->flank = FALLING;
        }
        else if (currentGate) {
            this->flank = HIGH;
        }
        else {
            this->flank = LOW;
        }
    }

    uint8_t GateSet::getPosition() const {
        return position;
    }

    Flank GateSet::getFlank() const {
        return flank;
    }

    void GateSet::reset() {
        position = 0;
        previousPosition = 0;
        flank = getInitFlank(this->gates);
    }

    bool GateSet::getGate() const {
        return gates[position];
    }

    Flank getInitFlank(std::vector<bool> gates) {
        if (gates.size() == 0) {
            printf("GateSet::getInitFlank: no gates\n");
            return LOW;
        }
        else {
            Flank result = gates[0] ? RISING : LOW;
            return result;
        }
    }

    GateSet GateSet::createEuclidean(uint8_t numSteps, uint8_t numPulses, uint8_t rotation, uint32_t patternLength) {
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

        // Expand the pattern to fill the requested pattern length
        std::vector<bool> expandedPattern(patternLength, false);
        if (!pattern.empty()) {
            uint32_t stepSize = patternLength / numSteps;
            for (uint32_t i = 0; i < patternLength; i++) {
                expandedPattern[i] = pattern[(i / stepSize) % numSteps];
            }
        }

        return GateSet(expandedPattern);
    }

} // namespace common
