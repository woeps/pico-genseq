#pragma once

#include "pico/multicore.h"

namespace commands {

    // Enum for sequencer commands
    enum class Command {
        PLAY,
        STOP,
        SET_BPM,
        ACTIVATE_PATTERN,
        DEACTIVATE_PATTERN,
        // Add more commands as needed
    };

    // Command message structure for inter-core communication
    struct CommandMessage {
        Command cmd;
        uint32_t param1;
        uint32_t param2;
    };

    // Function to send a command to the sequencer core
    void sendCommand(Command cmd, uint32_t param1 = 0, uint32_t param2 = 0);

} // namespace commands
