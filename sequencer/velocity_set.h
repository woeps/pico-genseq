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

private:
    std::vector<uint8_t> velocities;
};

} // namespace sequencer
