#pragma once

#include <vector>
#include <cstdint>

namespace common {

    // Playing order for pitch traversal
    enum class PlayingOrder : uint8_t {
        FORWARDS,
        BACKWARDS,
        PENDULUM,
        RANDOM,
    };

    // Class to represent a set of pitches (notes)
    class PitchSet {
    public:
        PitchSet(const std::vector<uint8_t>& pitches = {},
                 PlayingOrder order = PlayingOrder::FORWARDS);
        const std::vector<uint8_t>& getPitches() const;
        void setPitches(const std::vector<uint8_t>& pitches);

        PlayingOrder getOrder() const;
        void setOrder(PlayingOrder order);

        void setPosition(uint8_t position);
        uint8_t getPosition() const;

        uint8_t getPitch() const;
        uint8_t getPreviousPitch() const;

        void advance();
        void reset();

    private:
        std::vector<uint8_t> pitches;
        uint8_t position;
        uint8_t previousPosition;
        PlayingOrder order;
        int8_t pendulumDirection;
        uint32_t randomState;

        uint8_t lcgNext();
    };

} // namespace common
