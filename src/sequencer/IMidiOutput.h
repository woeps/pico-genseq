#pragma once

#include <cstdint>

namespace sequencer {

    // Abstract transport for MIDI bytes. Deliberately free of Pico SDK includes
    // so it compiles in any standard C++ toolchain (firmware or host tests).
    class IMidiOutput {
    public:
        virtual ~IMidiOutput() = default;

        // Write exactly one MIDI byte to the underlying transport, unaltered.
        virtual void write(uint8_t byte) = 0;
    };

} // namespace sequencer
