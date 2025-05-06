#include "pitch_set.h"

namespace sequencer {

    // PitchSet implementation
    PitchSet::PitchSet(const std::vector<uint8_t>& pitches) : pitches(pitches) {}

    const std::vector<uint8_t>& PitchSet::getPitches() const {
        return pitches;
    }

    void PitchSet::setPitches(const std::vector<uint8_t>& pitches) {
        this->pitches = pitches;
    }

} // namespace sequencer
