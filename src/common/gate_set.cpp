#include "gate_set.h"
#include <algorithm>
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

    Flank getInitFlank(const std::vector<bool>& gates);

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

    Flank getInitFlank(const std::vector<bool>& gates) {
        if (gates.size() == 0) {
            return LOW;
        }
        else {
            Flank result = gates[0] ? RISING : LOW;
            return result;
        }
    }

    GateSet GateSet::createEuclidean(uint8_t numSteps, uint8_t numPulses, uint8_t rotation,
                                     uint8_t stepLength, uint8_t gateLength) {
        // Euclidean algorithm (Bjorklund's algorithm). The step-level
        // decisions — which of the numSteps positions carry a pulse — come
        // from the shared euclideanPulseAt free function (gate_set.h);
        // GateSet keeps only the shared gate data handling: the tick-level
        // expansion (step length, gate length) below. The sequencer's
        // EuclideanGateGenerator plugin delegates to the same function, so
        // the plugin path and this legacy path are bit-identical by
        // construction.
        if (numSteps == 0 || stepLength == 0) return GateSet();
        std::vector<bool> pattern(numSteps, false);

        for (uint8_t step = 0; step < numSteps; step++) {
            pattern[step] = euclideanPulseAt(numSteps, numPulses, rotation, step);
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
