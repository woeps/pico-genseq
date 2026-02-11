#pragma once

#include <vector>
#include <cstdint>

namespace sequencer {

    enum Flank : uint8_t {
        LOW = 0,
        RISING = 1,
        HIGH = 2,
        FALLING = 3
    };

    // Class to represent a gate pattern (on/off values for each tick)
    class GateSet {
    public:
        GateSet(const std::vector<bool>& gates = {});
        const std::vector<bool>& getGates() const;
        void setGates(const std::vector<bool>& gates);
        
        // Get the total length of the pattern in ticks
        uint32_t getLength() const;

        void setPosition(uint8_t position);
        uint8_t getPosition() const;
        
        // Check if a gate is active at the given tick position
        Flank getFlank() const;

        bool getGate() const;

        void reset();

        /// Create a gate set based on the Euclidean algorithm (Bjorklund's algorithm)
        ///
        /// \param steps The total number of steps in the pattern
        /// \param pulses The number of pulses to distribute evenly
        /// \param rotation The rotation in steps to apply to the resulting pattern
        /// \param patternLength The total length of the pattern in ticks
        ///
        /// \return A GateSet with the generated pattern
        static GateSet createEuclidean(uint8_t steps, uint8_t pulses, uint8_t rotation, uint32_t patternLength);

    private:
        std::vector<bool> gates;
        uint8_t position;
        uint8_t previousPosition;
        Flank flank;
    };
} // namespace sequencer
