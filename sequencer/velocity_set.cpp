#include "velocity_set.h"
#include <cstdio>

namespace sequencer {

    // VelocitySet implementation
    VelocitySet::VelocitySet(const std::vector<uint8_t>& velocities) : velocities(velocities), position(0) {}

    const std::vector<uint8_t>& VelocitySet::getVelocities() const {
        return velocities;
    }

    void VelocitySet::setVelocities(const std::vector<uint8_t>& velocities) {
        this->velocities = velocities;
    }

    void VelocitySet::setPosition(uint8_t position) {
        if(position >= velocities.size()) {
            printf("VelocitySet::setPosition: position %d is out of bounds for velocity set of size %d\n", position, velocities.size());
        }
        this->position = position % velocities.size();
    }

    uint8_t VelocitySet::getPosition() const {
        return position;
    }

    uint8_t VelocitySet::getVelocity() const {
        return velocities[position];
    }

    void VelocitySet::reset() {
        position = 0;
    }
} // namespace sequencer
