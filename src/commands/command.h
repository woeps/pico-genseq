#pragma once

#include <cstdint>
#include <vector>
#include "pico/multicore.h"
#include "../common/pitch_set.h"

namespace sequencer { class PatternSnapshot; }  // fwd (task 6.4 receive path)

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
        PATTERN_PITCH_SET,
        PATTERN_VELOCITY_SET,
        PATTERN_ADD,
        PATTERN_REMOVE,
        // Add more commands as needed
    };

    constexpr uint16_t MAX_GATE_SET_LENGTH = 64 * 96;
    constexpr uint8_t MAX_PITCH_SET_LENGTH = 16;

    // Command message structure for inter-core communication
    struct CommandMessage {
        Command cmd = Command::NOOP;
        uint8_t param1 = 0;
        uint8_t param2 = 0;
        uint16_t gateCount = 0;
        std::vector<bool> gates{};
        uint8_t pitchCount = 0;
        common::PlayingOrder pitchOrder = common::PlayingOrder::FORWARDS;
        std::vector<uint8_t> pitches{};
        uint8_t velocityCount = 0;
        common::PlayingOrder velocityOrder = common::PlayingOrder::FORWARDS;
        std::vector<uint8_t> velocities{};
    };

    // Function to send a command to the sequencer core
    void sendCommand(Command cmd, uint8_t param1 = 0, uint8_t param2 = 0);
    void sendGateSet(uint8_t patternIndex, const std::vector<bool>& gates);
    void sendPitchSet(uint8_t patternIndex, uint8_t count,
                      common::PlayingOrder order, const std::vector<uint8_t>& pitches);
    void sendVelocitySet(uint8_t patternIndex, uint8_t count,
                         common::PlayingOrder order,
                         const std::vector<uint8_t>& velocities);

    /**
     * @brief Receive a command message from the UI core
     * 
     * @return CommandMessage with the received command and parameters
     */
    CommandMessage receiveCommand();

    /**
     * @brief Allocation-free receive for the timing-critical core1 path
     *        (performance-safe-foundation task 6.4, Req 7.4/7.9/7.10).
     *
     * Decodes one FIFO command exactly like receiveCommand(), but for the
     * multiword pattern-set commands (GATE/PITCH/VELOCITY) it writes the payload
     * into the caller-owned, reused @p snapshot instead of heap-allocating the
     * std::vector members of CommandMessage via .assign(). The returned
     * CommandMessage carries only the SCALAR fields (cmd, param1, counts, order)
     * — its vector members stay empty — so the caller applies set commands from
     * @p snapshot (see Sequencer::processCommand overload). This removes the
     * only steady-state heap allocation on the core1 receive path.
     *
     * @param snapshot  Reused fixed-capacity decode buffer. CLEARED on entry.
     * @return CommandMessage with scalar fields set; vector members empty.
     */
    CommandMessage receiveCommandInto(sequencer::PatternSnapshot& snapshot);

} // namespace commands
