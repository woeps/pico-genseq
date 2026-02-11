#pragma once

#include <vector>
#include <cstdint>

namespace sequencer {

    // Class to represent a set of pitches (notes)
    class PitchSet {
    public:
        PitchSet(const std::vector<uint8_t>& pitches = {});
        const std::vector<uint8_t>& getPitches() const;
        void setPitches(const std::vector<uint8_t>& pitches);

        void setPosition(uint8_t position);
        uint8_t getPosition() const;

        uint8_t getPitch() const;
        uint8_t getPreviousPitch() const;

        void reset();

    private:
        std::vector<uint8_t> pitches;
        uint8_t position;
        uint8_t previousPosition;
    };

} // namespace sequencer
