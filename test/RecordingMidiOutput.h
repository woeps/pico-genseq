#pragma once

#include <cstdint>
#include <vector>
#include "../src/sequencer/IMidiOutput.h"

// Host-only recording implementation of IMidiOutput for unit/property tests.
// Appends every received byte to an ordered, retrievable buffer.
class RecordingMidiOutput : public sequencer::IMidiOutput {
public:
    void write(uint8_t byte) override { bytes_.push_back(byte); }
    const std::vector<uint8_t>& record() const { return bytes_; }
    void clear() { bytes_.clear(); }

private:
    std::vector<uint8_t> bytes_;
};
