#pragma once

#include <cstdint>
#include <vector>
#include "pico/multicore.h"
#include "../common/pitch_set.h"
#include "../common/GenerationParameters.h"

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
        GEN_PARAMS_SET,  // generative-engine-expansion: apply a pattern's Generation Parameters
        // set-library fan-out: core1 holds ticks while a batch of per-pattern set updates
        // lands, so patterns sharing one edited set never play mixed old/new material.
        // BATCH_BEGIN param1 = number of commands that follow (1..MAX_BATCH_COMMANDS).
        BATCH_BEGIN,
        BATCH_END,
        // set-library multicast: one set payload applied to every pattern in a 16-bit mask.
        // Wire: header word (same layout as the single-pattern command, pattern byte unused),
        // then the mask word, then the payload. Sent when a set has more than one user, so the
        // payload is expanded, transferred and decoded once instead of once per pattern.
        PATTERN_GATE_SET_MULTI,
        PATTERN_PITCH_SET_MULTI,
        PATTERN_VELOCITY_SET_MULTI,
        // Instrument routing: replace one route slot. Wire: header word (param1 = slot,
        // param2 = protocol channel 0..15), then the mask word (effective set of patterns that
        // sound on the slot: members AND NOT muted, folded by core0). One command is
        // atomic on core1, so no batch frame is needed.
        INSTRUMENT_SET,
        // Add more commands as needed
    };

    // 15 patterns x (gate, pitch, velocity, gen params).
    constexpr uint8_t MAX_BATCH_COMMANDS = 60;

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
        uint16_t patternMask = 0;  // *_SET_MULTI: bit p set == apply to pattern p
    };

    inline bool isMultiSetCommand(Command c) {
        return c == Command::PATTERN_GATE_SET_MULTI || c == Command::PATTERN_PITCH_SET_MULTI ||
               c == Command::PATTERN_VELOCITY_SET_MULTI;
    }

    // Commands whose header word is followed by a 16-bit mask word.
    inline bool hasMaskWord(Command c) {
        return isMultiSetCommand(c) || c == Command::INSTRUMENT_SET;
    }

    // Function to send a command to the sequencer core
    void sendCommand(Command cmd, uint8_t param1 = 0, uint8_t param2 = 0);
    void sendGateSet(uint8_t patternIndex, const std::vector<bool>& gates);
    void sendPitchSet(uint8_t patternIndex, uint8_t count,
                      common::PlayingOrder order, const std::vector<uint8_t>& pitches);
    void sendVelocitySet(uint8_t patternIndex, uint8_t count,
                         common::PlayingOrder order,
                         const std::vector<uint8_t>& velocities);

    // Replace Instrument route slot `slot` (0..15) with `protocolChannel` (0..15) and the
    // effective pattern `patternMask`.
    void sendInstrumentSet(uint8_t slot, uint8_t protocolChannel, uint16_t patternMask);

    // Multicast variants (set-library fan-out): apply one set to every pattern in `patternMask`.
    void sendGateSetMulti(uint16_t patternMask, const std::vector<bool>& gates);
    void sendPitchSetMulti(uint16_t patternMask, uint8_t count,
                           common::PlayingOrder order, const std::vector<uint8_t>& pitches);
    void sendVelocitySetMulti(uint16_t patternMask, uint8_t count,
                              common::PlayingOrder order,
                              const std::vector<uint8_t>& velocities);

    // generative-engine-expansion task 10.4 — send a pattern's Generation
    // Parameters to core1 over the FIFO. Fixed 7-word payload (no per-element
    // count), decoded into the PatternSnapshot's GenerationParameters by
    // receiveCommandInto / receiveCommand. This is the single cross-core path
    // that carries scale / probability / humanize / seed / coupling / stepLength.
    void sendGenParams(uint8_t patternIndex, const common::GenerationParameters& params);

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
