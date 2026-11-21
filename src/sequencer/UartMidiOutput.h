#pragma once

#include <cstdint>
#include "hardware/uart.h"
#include "IMidiOutput.h"

namespace sequencer {

    // MIDI serial rate (moved here from sequencer.h; this is the only
    // component that needs it).
    static constexpr uint MIDI_BAUD_RATE = 31250;

    // Direct UART MIDI output. write() blocks while the UART FIFO is full. It is also the Sink of
    // QueuedMidiOutput (MidiByteQueue.h): txEmpty() / put() let the queue refill the FIFO without
    // ever waiting.
    class UartMidiOutput : public IMidiOutput {
    public:
        UartMidiOutput(uart_inst_t* uart, uint txPin, uint rxPin);
        void write(uint8_t byte) override;

        // Sink interface: the transmit FIFO is completely empty / write one byte to it.
        bool txEmpty() const;
        void put(uint8_t byte);

    private:
        uart_inst_t* uart;
    };

} // namespace sequencer
