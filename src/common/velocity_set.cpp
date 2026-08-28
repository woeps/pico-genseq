#include "velocity_set.h"
#include <cstdio>

namespace common {

    VelocitySet::VelocitySet(const std::vector<uint8_t>& velocities, PlayingOrder order)
        : velocities(velocities), position(0), previousPosition(0),
          order(order), pendulumDirection(1), randomState(0x87654321) {}

    const std::vector<uint8_t>& VelocitySet::getVelocities() const {
        return velocities;
    }

    void VelocitySet::setVelocities(const std::vector<uint8_t>& velocities) {
        this->velocities = velocities;
        this->position = 0;
        this->previousPosition = 0;
        this->pendulumDirection = 1;
    }

    PlayingOrder VelocitySet::getOrder() const {
        return order;
    }

    void VelocitySet::setOrder(PlayingOrder order) {
        this->order = order;
        this->pendulumDirection = 1;
    }

    void VelocitySet::setPosition(uint8_t position) {
        if (velocities.empty()) {
            this->position = 0;
            this->previousPosition = 0;
            return;
        }
        if (position >= velocities.size()) {
            printf("VelocitySet::setPosition: position %d is out of bounds for velocity set of size %zu\n",
                   position, velocities.size());
        }
        this->previousPosition = this->position;
        this->position = position % velocities.size();
    }

    uint8_t VelocitySet::getPosition() const {
        return position;
    }

    uint8_t VelocitySet::getVelocity() const {
        if (velocities.empty()) return 0;
        return velocities[position];
    }

    uint8_t VelocitySet::lcgNext() {
        randomState = randomState * 1103515245u + 12345u;
        return static_cast<uint8_t>((randomState >> 16) & 0xFF);
    }

    void VelocitySet::advance() {
        const uint8_t size = static_cast<uint8_t>(velocities.size());
        if (size == 0) return;
        previousPosition = position;
        if (size == 1) {
            position = 0;
            return;
        }
        switch (order) {
            case PlayingOrder::FORWARDS:
                position = (position + 1) % size;
                break;
            case PlayingOrder::BACKWARDS:
                position = (position + size - 1) % size;
                break;
            case PlayingOrder::PENDULUM:
                if (pendulumDirection > 0 && position >= size - 1) {
                    pendulumDirection = -1;
                } else if (pendulumDirection < 0 && position == 0) {
                    pendulumDirection = 1;
                }
                position = static_cast<uint8_t>(position + pendulumDirection);
                break;
            case PlayingOrder::RANDOM: {
                position = lcgNext() % size;
                break;
            }
        }
    }

    void VelocitySet::reset() {
        position = 0;
        previousPosition = 0;
        pendulumDirection = 1;
    }

} // namespace common
