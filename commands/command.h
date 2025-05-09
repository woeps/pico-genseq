#pragma once

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
        PATTERN_EUCLIDEAN_SET_STEPS,
        PATTERN_EUCLIDEAN_SET_PULSES,
        PATTERN_EUCLIDEAN_SET_ROTATION,
        PATTERN_EUCLIDEAN_SET_LENGTH,
        // Add more commands as needed
    };

    // Command message structure for inter-core communication
    struct CommandMessage {
        Command cmd;
        uint8_t param1;
        uint8_t param2;
    };

    // Function to send a command to the sequencer core
    void sendCommand(Command cmd, uint8_t param1 = 0, uint8_t param2 = 0);

    /**
     * @brief Receive a command message from the UI core
     * 
     * @return CommandMessage with the received command and parameters
     */
    CommandMessage receiveCommand();

} // namespace commands
