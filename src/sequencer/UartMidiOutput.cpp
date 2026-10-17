#include "UartMidiOutput.h"
#include "hardware/gpio.h"

namespace sequencer {

    UartMidiOutput::UartMidiOutput(uart_inst_t* uart, uint txPin, uint rxPin)
        : uart(uart) {
        // Init at MIDI baud FIRST, then configure pins (Req 2.3 ordering).
        uart_init(uart, MIDI_BAUD_RATE);
        gpio_set_function(txPin, GPIO_FUNC_UART); // TX before RX (Req 2.3)
        gpio_set_function(rxPin, GPIO_FUNC_UART);
    }

    void UartMidiOutput::write(uint8_t byte) {
        uart_putc_raw(uart, byte); // exactly one byte, unmodified (Req 2.2, 2.4)
    }

} // namespace sequencer
