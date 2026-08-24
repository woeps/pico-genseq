#include <algorithm>
#include <stdio.h>
#include "command.h"
#include "pico/multicore.h"

namespace commands {

    // Function to send a command to the sequencer core
    void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
        // Pack command and parameters into a single 32-bit value
        uint32_t packed = static_cast<uint32_t>(cmd) |
            (static_cast<uint32_t>(param1) << 8) |
            (static_cast<uint32_t>(param2) << 16);
        
        printf("Sending command: %d, param1: %d, param2: %d\n", static_cast<int>(cmd), param1, param2);

        // Send to the sequencer core
        multicore_fifo_push_blocking(packed);
    }

    void sendGateSet(uint8_t patternIndex, const std::vector<bool>& gates) {
        const uint16_t gateCount = static_cast<uint16_t>(
            std::min(gates.size(), static_cast<size_t>(MAX_GATE_SET_LENGTH)));
        const uint32_t packed = static_cast<uint32_t>(Command::PATTERN_GATE_SET) |
            (static_cast<uint32_t>(patternIndex) << 8) |
            (static_cast<uint32_t>(gateCount) << 16);
        multicore_fifo_push_blocking(packed);
        for (uint16_t wordIndex = 0; wordIndex < (gateCount + 31) / 32; wordIndex++) {
            uint32_t word = 0;
            for (uint8_t bit = 0; bit < 32; bit++) {
                const size_t gateIndex = static_cast<size_t>(wordIndex) * 32 + bit;
                if (gateIndex < gateCount && gates[gateIndex]) word |= 1u << bit;
            }
            multicore_fifo_push_blocking(word);
        }
    }

    CommandMessage receiveCommand() {
        commands::CommandMessage msg;
        if (multicore_fifo_rvalid()) {
            uint32_t raw_cmd = multicore_fifo_pop_blocking();
            msg.cmd = static_cast<commands::Command>(raw_cmd & 0xFF);
            msg.param1 = (raw_cmd >> 8) & 0xFF;
            msg.param2 = (raw_cmd >> 16) & 0xFF;
            if (msg.cmd == Command::PATTERN_GATE_SET) {
                const uint16_t receivedGateCount = raw_cmd >> 16;
                msg.gateCount = std::min(receivedGateCount, MAX_GATE_SET_LENGTH);
                msg.gates.assign(msg.gateCount, false);
                const uint16_t wordCount = (receivedGateCount + 31) / 32;
                for (uint16_t i = 0; i < wordCount; i++) {
                    const uint32_t word = multicore_fifo_pop_blocking();
                    for (uint8_t bit = 0; bit < 32; bit++) {
                        const size_t gateIndex = static_cast<size_t>(i) * 32 + bit;
                        if (gateIndex < msg.gateCount) msg.gates[gateIndex] = word & (1u << bit);
                    }
                }
            }
            if (msg.cmd == Command::PATTERN_GATE_SET) {
                printf("Receiving gate set: pattern: %d, gates: %d\n", msg.param1, msg.gateCount);
            } else {
                printf("Receiving command: %d, param1: %d, param2: %d\n",
                       static_cast<int>(msg.cmd), msg.param1, msg.param2);
            }
        }
        return msg;
    }

} // namespace commands
