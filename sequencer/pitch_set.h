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

    private:
        std::vector<uint8_t> pitches;
    };

} // namespace sequencer
