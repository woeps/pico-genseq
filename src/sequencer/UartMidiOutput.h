#pragma once

#include <cstdint>
#include "hardware/uart.h"
#include "IMidiOutput.h"

namespace sequencer {

    // MIDI serial rate (moved here from sequencer.h; this is the only
    // component that needs it).
    static constexpr uint MIDI_BAUD_RATE = 31250;

    class UartMidiOutput : public IMidiOutput {
    public:
        UartMidiOutput(uart_inst_t* uart, uint txPin, uint rxPin);
        void write(uint8_t byte) override;

    private:
        uart_inst_t* uart;
    };

} // namespace sequencer
