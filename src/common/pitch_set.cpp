#include "pitch_set.h"
#include <cstdio>

namespace common {

    // PitchSet implementation
    PitchSet::PitchSet(const std::vector<uint8_t>& pitches, PlayingOrder order)
        : pitches(pitches), position(0), previousPosition(0),
          order(order), pendulumDirection(1), randomState(0x12345678) {}

    const std::vector<uint8_t>& PitchSet::getPitches() const {
        return this->pitches;
    }

    void PitchSet::setPitches(const std::vector<uint8_t>& pitches) {
        this->pitches = pitches;
        this->position = 0;
        this->previousPosition = 0;
        this->pendulumDirection = 1;
    }

    PlayingOrder PitchSet::getOrder() const {
        return this->order;
    }

    void PitchSet::setOrder(PlayingOrder order) {
        this->order = order;
        this->pendulumDirection = 1;
    }

    void PitchSet::setPosition(uint8_t position) {
        if (pitches.empty()) {
            this->position = 0;
            this->previousPosition = 0;
            return;
        }
        if(position >= pitches.size()) {
            printf("PitchSet::setPosition: position %d is out of bounds for pitch set of size %zu\n", position, pitches.size());
        }
        this->previousPosition = this->position;
        this->position = position % pitches.size();
    }

    uint8_t PitchSet::getPosition() const {
        return this->position;
    }

    uint8_t PitchSet::getPitch() const {
        if (pitches.empty()) return 0;
        return this->pitches[this->position];
    }

    uint8_t PitchSet::getPreviousPitch() const {
        if (pitches.empty()) return 0;
        return this->pitches[this->previousPosition];
    }

    uint8_t PitchSet::lcgNext() {
        randomState = randomState * 1103515245u + 12345u;
        return static_cast<uint8_t>((randomState >> 16) & 0xFF);
    }

    void PitchSet::advance() {
        const uint8_t size = static_cast<uint8_t>(pitches.size());
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

    void PitchSet::reset() {
        this->position = 0;
        this->previousPosition = 0;
        this->pendulumDirection = 1;
    }

} // namespace common
