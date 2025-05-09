#include <stdio.h>
#include "command.h"
#include "pico/multicore.h"

namespace commands {

    // Function to send a command to the sequencer core
    void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
        // Pack command and parameters into a single 32-bit value
        uint32_t packed = static_cast<uint32_t>(cmd) |
            (param1 << 8) |
            (param2 << 16);
        
            printf("Sending command: %d, param1: %d, param2: %d\n", cmd, param1, param2);

        // Send to the sequencer core
        multicore_fifo_push_blocking(packed);
    }

    CommandMessage receiveCommand() {
        commands::CommandMessage msg = {commands::Command::NOOP, 0, 0};
        if (multicore_fifo_rvalid()) {
            uint32_t raw_cmd = multicore_fifo_pop_blocking();
            msg.cmd = static_cast<commands::Command>(raw_cmd & 0xFF);
            msg.param1 = (raw_cmd >> 8) & 0xFFFF;
            msg.param2 = (raw_cmd >> 16) & 0xFF;
            printf("Receiving command: %d, param1: %d, param2: %d\n", msg.cmd, msg.param1, msg.param2);
        }
        return msg;
    }

} // namespace commands
