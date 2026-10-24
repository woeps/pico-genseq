#pragma once

#include <vector>
#include <cstdint>

namespace common {

    enum Flank : uint8_t {
        LOW = 0,
        RISING = 1,
        HIGH = 2,
        FALLING = 3
    };

    /// The Euclidean pulse decision (Bjorklund's algorithm): does pattern
    /// position `position` carry a pulse in the E(pulses, steps) pattern
    /// rotated by `rotation` steps?
    ///
    /// This is the historical bucket algorithm that lived inside
    /// GateSet::createEuclidean, exposed as a reusable free function
    /// (generative-engine-expansion task 4) so the algorithm is SHARED
    /// rather than duplicated: GateSet::createEuclidean and the
    /// sequencer::EuclideanGateGenerator plugin both delegate here.
    ///
    /// Total for every uint8 configuration, keeping the historical
    /// semantics exactly:
    ///   - steps == 0 or pulses == 0 -> no pulse at any position;
    ///   - pulses >= steps            -> a pulse at every position;
    ///   - otherwise position and rotation wrap modulo steps.
    inline bool euclideanPulseAt(uint8_t steps, uint8_t pulses, uint8_t rotation,
                                 uint32_t position) {
        if (steps == 0 || pulses == 0) return false;
        if (pulses >= steps) return true;
        // The historical loop marked position (i + rotation) % steps when the
        // pulse bucket for i crossed a step boundary, i.e. when
        // ((i + 1) * pulses) mod steps < pulses. Invert the rotation to find
        // the bucket index for this position, then apply the same condition.
        const uint32_t r = static_cast<uint32_t>(rotation) % steps;
        const uint32_t i = (position % steps + steps - r) % steps;
        // (i + 1) * pulses <= 255 * 255: fits in 32 bits, no overflow.
        const uint32_t bucket = (i + 1u) * static_cast<uint32_t>(pulses);
        return (bucket % steps) < static_cast<uint32_t>(pulses);
    }

    // Class to represent a gate pattern (on/off values for each tick)
    class GateSet {
    public:
        GateSet(const std::vector<bool>& gates = {});
        const std::vector<bool>& getGates() const;
        void setGates(const std::vector<bool>& gates);
        
        // Get the total length of the pattern in ticks
        uint32_t getLength() const;

        void setPosition(uint32_t position);
        uint32_t getPosition() const;
        
        // Check if a gate is active at the given tick position
        Flank getFlank() const;

        bool getGate() const;

        void reset();

        /// Create a gate set based on the Euclidean algorithm (Bjorklund's algorithm).
        /// The step-level pulse decisions — which of the `steps` pattern positions
        /// carry a pulse — come from the shared euclideanPulseAt free function
        /// above (one algorithm, also used by the sequencer::EuclideanGateGenerator
        /// plugin, which delegates to it); this class keeps only the shared gate
        /// data handling: the tick-level expansion (step length, gate length).
        ///
        /// \param steps The total number of steps in the pattern
        /// \param pulses The number of pulses to distribute evenly
        /// \param rotation The rotation in steps to apply to the resulting pattern
        /// \param stepLength The length of each step in ticks
        /// \param gateLength The gate length as a percentage of the interval until the next pulse
        ///
        /// \return A GateSet with the generated pattern
        static GateSet createEuclidean(uint8_t steps, uint8_t pulses, uint8_t rotation,
                                       uint8_t stepLength, uint8_t gateLength);

    private:
        std::vector<bool> gates;
        uint32_t position;
        uint32_t previousPosition;
        Flank flank;
    };
} // namespace common
