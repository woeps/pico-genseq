#include "velocity_set.h"

namespace sequencer {

    // VelocitySet implementation
    VelocitySet::VelocitySet(const std::vector<uint8_t>& velocities) : velocities(velocities) {}

    const std::vector<uint8_t>& VelocitySet::getVelocities() const {
        return velocities;
    }

    void VelocitySet::setVelocities(const std::vector<uint8_t>& velocities) {
        this->velocities = velocities;
    }

} // namespace sequencer
