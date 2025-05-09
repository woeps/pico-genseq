#pragma once

#include <vector>
#include <cstdint>

namespace sequencer {

    // Class to represent a set of velocities
    class VelocitySet {
    public:
        VelocitySet(const std::vector<uint8_t>& velocities = {});
        const std::vector<uint8_t>& getVelocities() const;
        void setVelocities(const std::vector<uint8_t>& velocities);

        void setPosition(uint8_t position);
        uint8_t getPosition() const;

        uint8_t getVelocity() const;

        void reset();

    private:
        std::vector<uint8_t> velocities;
        uint8_t position;
    };

} // namespace sequencer
