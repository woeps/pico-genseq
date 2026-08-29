#pragma once

#include <vector>
#include <cstdint>
#include "pitch_set.h"

namespace common {

    // Class to represent a set of velocities
    class VelocitySet {
    public:
        VelocitySet(const std::vector<uint8_t>& velocities = {},
                    PlayingOrder order = PlayingOrder::FORWARDS);
        const std::vector<uint8_t>& getVelocities() const;
        void setVelocities(const std::vector<uint8_t>& velocities);

        PlayingOrder getOrder() const;
        void setOrder(PlayingOrder order);

        void setPosition(uint8_t position);
        uint8_t getPosition() const;

        uint8_t getVelocity() const;

        void advance();
        void reset();

    private:
        std::vector<uint8_t> velocities;
        uint8_t position;
        uint8_t previousPosition;
        PlayingOrder order;
        int8_t pendulumDirection;
        uint32_t randomState;

        uint8_t lcgNext();
    };

} // namespace common
