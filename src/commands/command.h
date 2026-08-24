#pragma once

#include <cstdint>
#include <vector>
#include "pico/multicore.h"

namespace commands {

    // Enum for sequencer commands
    enum class Command {
        NOOP,
        PLAY,
        STOP,
        BPM_SET,
        PATTERN_ACTIVATE,
        PATTERN_DEACTIVATE,
        PATTERN_GATE_SET,
        PATTERN_ADD,
        PATTERN_REMOVE,
        // Add more commands as needed
    };

    constexpr uint16_t MAX_GATE_SET_LENGTH = 64 * 96;

    // Command message structure for inter-core communication
    struct CommandMessage {
        Command cmd = Command::NOOP;
        uint8_t param1 = 0;
        uint8_t param2 = 0;
        uint16_t gateCount = 0;
        std::vector<bool> gates{};
    };

    // Function to send a command to the sequencer core
    void sendCommand(Command cmd, uint8_t param1 = 0, uint8_t param2 = 0);
    void sendGateSet(uint8_t patternIndex, const std::vector<bool>& gates);

    /**
     * @brief Receive a command message from the UI core
     * 
     * @return CommandMessage with the received command and parameters
     */
    CommandMessage receiveCommand();

} // namespace commands
