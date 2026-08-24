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

    void GateSet::setPosition(uint32_t position) {
        if (position >= gates.size()) {
            printf("GateSet::setPosition: position %lu is out of bounds for gate set of size %zu\n",
                   static_cast<unsigned long>(position), gates.size());
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

    uint32_t GateSet::getPosition() const {
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

    GateSet GateSet::createEuclidean(uint8_t numSteps, uint8_t numPulses, uint8_t rotation,
                                     uint8_t stepLength, uint8_t gateLength) {
        // Euclidean algorithm implementation (Bjorklund's algorithm)
        if (numSteps == 0 || stepLength == 0) return GateSet();
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

        const uint32_t patternLength = static_cast<uint32_t>(numSteps) * stepLength;
        std::vector<bool> expandedPattern(patternLength, false);
        std::vector<uint32_t> pulsePositions;
        for (uint8_t step = 0; step < numSteps; step++) {
            if (pattern[step]) pulsePositions.push_back(static_cast<uint32_t>(step) * stepLength);
        }

        gateLength = std::min<uint8_t>(gateLength, 100);
        for (size_t i = 0; i < pulsePositions.size(); i++) {
            const uint32_t start = pulsePositions[i];
            const uint32_t next = pulsePositions[(i + 1) % pulsePositions.size()];
            const uint32_t interval = next > start ? next - start : patternLength - start + next;
            const uint32_t scaled = (interval * gateLength + 99) / 100;
            const uint32_t highTicks = std::max<uint32_t>(
                1, std::min<uint32_t>(interval - 1, scaled));
            for (uint32_t tick = 0; tick < highTicks; tick++) {
                expandedPattern[(start + tick) % patternLength] = true;
            }
        }

        return GateSet(expandedPattern);
    }

} // namespace common
