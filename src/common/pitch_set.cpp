#include "pitch_set.h"
#include <cstdio>

namespace common {

    // PitchSet implementation
    PitchSet::PitchSet(const std::vector<uint8_t>& pitches) : pitches(pitches), position(0), previousPosition(0) {}

    const std::vector<uint8_t>& PitchSet::getPitches() const {
        return this->pitches;
    }

    void PitchSet::setPitches(const std::vector<uint8_t>& pitches) {
        this->pitches = pitches;
    }

    void PitchSet::setPosition(uint8_t position) {
        if(position >= pitches.size()) {
            printf("PitchSet::setPosition: position %d is out of bounds for pitch set of size %d\n", position, pitches.size());
        }
        this->previousPosition = this->position;
        this->position = position % pitches.size();
    }

    uint8_t PitchSet::getPosition() const {
        return this->position;
    }

    uint8_t PitchSet::getPitch() const {
        return this->pitches[this->position];
    }

    uint8_t PitchSet::getPreviousPitch() const {
        return this->pitches[this->previousPosition];
    }

    void PitchSet::reset() {
        this->position = 0;
        this->previousPosition = 0;
    }

} // namespace common
