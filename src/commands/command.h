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
        SCALE_SET,          // global scale (root + template), single-word, like BPM_SET
        PATTERN_SCALE_SET,  // per-pattern scale override, single-word
        GEN_PARAM_SET,      // one generator parameter write (ui-integration T5),
                            // two words: header + 16-bit value
        // The generative mutation/recovery commands (ui-integration T6):
        // the MutationView's ONLY cross-core path — the view holds no
        // registry and no inbox of the sequencer core; every request rides
        // the command queue. MUTATE names the operator by its INDEX into
        // the built-in registration order (both cores list the same order
        // independently) and the pattern by its slot index (or the
        // ALL_PATTERNS sentinel 0xFF); UNDO / REVERT / KEEP carry the
        // recovery gestures. All four carry the packed FR10 level-1 timing
        // override (see packGenerativeTiming below).
        MUTATE,
        UNDO,
        REVERT,
        KEEP,
        // The modulation target configuration commands (ui-integration T7):
        // the ModulationPatternView's ONLY cross-core path. The view holds NO
        // ModulationRegistry and NO ParameterBus — it keeps its OWN local UI
        // mirror of the configuration and posts every edit over the command
        // queue. MOD_TARGET_ADD claims a target slot bound to one of the
        // fixed global modulation patterns; MOD_TARGET_SET_PARAM writes ONE
        // field of one target slot (see ModTargetField). Both are SINGLE
        // FIFO WORDS whose payload rides the two generic payload bytes every
        // receive path already decodes, so command.cpp needs no changes.
        MOD_TARGET_ADD,
        MOD_TARGET_SET_PARAM,
        // Add more commands as needed
    };

    constexpr uint16_t MAX_GATE_SET_LENGTH = 64 * 96;
    constexpr uint8_t MAX_PITCH_SET_LENGTH = 16;

    // Command message structure for inter-core communication
    struct CommandMessage {
        Command cmd = Command::NOOP;
        uint8_t param1 = 0;
        uint8_t param2 = 0;
        // GEN_PARAM_SET payload (ui-integration T5):
        //   param1 = patternIndex, param2 = paramId, paramValue = the new
        //   value (the command's second FIFO word, 16-bit two's complement)
        int16_t paramValue = 0;
        // Scale payload, decoded by the receive paths for the scale commands
        // (ui-integration T1c):
        //   SCALE_SET:         param1 = scaleRoot,        param2 = scaleTemplateIndex
        //   PATTERN_SCALE_SET: param1 = patternIndex,     param2 = scaleRoot,
        //                      scaleTemplateIndex = top byte of the packed word
        uint8_t scaleRoot = 0;
        uint8_t scaleTemplateIndex = 0;
        // Generative payload (ui-integration T6), decoded by the receive
        // paths for the four generative commands:
        //   MUTATE:  param1 = operator index, param2 = pattern index
        //            (ALL_PATTERNS = every live pattern), mutateIntensity =
        //            the FR6 intensity 0-127 (the header's top byte),
        //            mutateTargets = the FR7 target mask and genTiming = the
        //            packed FR10 level-1 override (the second FIFO word)
        //   UNDO / REVERT / KEEP: param1 = the packed level-1 override
        uint8_t mutateIntensity = 0;
        uint16_t mutateTargets = 0;
        uint8_t genTiming = 0;
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
     * @brief Send the global scale (root pitch class + template index) to the
     *        sequencer core. The scale is a true global (like BPM) stored in a
     *        single source on core1; single-word wire format:
     *          cmd | scaleRoot<<8 | scaleTemplateIndex<<16
     */
    void sendScaleCommand(uint8_t root, uint8_t templateIndex);

    /**
     * @brief Send a per-pattern scale override to the sequencer core.
     *        Single-word wire format:
     *          cmd | patternIndex<<8 | scaleRoot<<16 | scaleTemplateIndex<<24
     */
    void sendPatternScaleCommand(uint8_t patternIndex, uint8_t root,
                                 uint8_t templateIndex);

    /**
     * @brief Send one generator parameter write to the sequencer core
     *        (ui-integration T5). The UI never touches the ParameterBus
     *        directly; every generator-parameter edit rides the command
     *        queue. Two FIFO words:
     *          GEN_PARAM_SET | patternIndex<<8 | paramId<<16
     *          value (16-bit two's complement)
     *
     * @param patternIndex  the pattern slot the parameter addresses
     * @param paramId       the ParameterBus ParamId (task-16 table)
     * @param value         the new value (already UI-clamped by the view)
     */
    void sendGenParamCommand(uint8_t patternIndex, uint8_t paramId, int16_t value);

    // The all-patterns sentinel for the MUTATE wire (ui-integration T6):
    // one command fans out over every live pattern slot on the sequencer
    // core. Mirrors sequencer::kAllPatterns (CommandInbox.h) without
    // sharing that header's cross-core types into the UI-facing command
    // vocabulary.
    constexpr uint8_t ALL_PATTERNS = 0xFFu;

    // Pack / unpack the FR10 level-1 timing override carried by the four
    // generative commands: bit 0 = active, bits 1-2 = the
    // OperationTimingMode ordinal (0 Immediate, 1 QuantizeBeat,
    // 2 QuantizeBar — OperationTimingConfig.h). An absent override packs to
    // 0, the wire default (the operation's level-2 mode and the level-3
    // global default then resolve the application boundary).
    constexpr uint8_t packGenerativeTiming(bool active, uint8_t modeOrdinal) {
        return static_cast<uint8_t>((active ? 1u : 0u) | (modeOrdinal << 1));
    }
    constexpr bool generativeTimingActive(uint8_t packed) {
        return (packed & 0x1u) != 0;
    }
    constexpr uint8_t generativeTimingMode(uint8_t packed) {
        return static_cast<uint8_t>((packed >> 1) & 0x3u);
    }

    /**
     * @brief Post one mutation request to the sequencer core
     *        (ui-integration T6). Two FIFO words:
     *          MUTATE | operatorIndex<<8 | patternIndex<<16 | intensity<<24
     *          targets | genTiming<<16
     *
     * @param operatorIndex  the index into the built-in registration order
     *                       (the sequencer resolves it through its OWN
     *                       registry; no registry crosses the cores)
     * @param patternIndex   the pattern slot, or ALL_PATTERNS for the
     *                       all-patterns fan-out
     * @param targets        the FR7 MutationTargetMask
     * @param intensity      the FR6 intensity 0-127
     * @param timing         the packed FR10 level-1 override
     *                       (packGenerativeTiming; 0 = absent)
     */
    void sendMutateCommand(uint8_t operatorIndex, uint8_t patternIndex,
                           uint16_t targets, uint8_t intensity, uint8_t timing = 0);

    /**
     * @brief Post one recovery gesture (ui-integration T6): Undo pops the
     *        global undo stack, Revert restores every pattern's baseline and
     *        clears the stack, Keep promotes the working state into the new
     *        baseline. Single FIFO word:
     *          UNDO|REVERT|KEEP | timing<<8
     */
    void sendUndoCommand(uint8_t timing = 0);
    void sendRevertCommand(uint8_t timing = 0);
    void sendKeepCommand(uint8_t timing = 0);

    // The per-target configuration fields addressable by MOD_TARGET_SET_PARAM
    // (ui-integration T7, FR9). The ordinals are CROSS-CORE PROTOCOL VALUES:
    // the UI view and the sequencer handler both map them to the SAME
    // ModulationTarget members (parameter, scope, scale, balance, duration,
    // order), but each side resolves them against its OWN state — no target
    // object and no registry crosses the cores.
    enum class ModTargetField : uint8_t {
        ParamId = 0,            // task-16 table id of the modulated parameter
        Scope = 1,              // ParameterScope ordinal (0 Pattern, 1 Global)
        AddressPatternSlot = 2, // the per-pattern slot for Pattern-scope addresses
        PatternIndex = 3,       // the driving global modulation pattern
        ScaleFactor = 4,        // FR14 scaling factor 0-127
        Balance = 5,            // FR14 balance, SIGNED byte (-127..127)
        DurationLength = 6,     // FR14 step duration length 1-16
        DurationUnit = 7,      // StepUnit ordinal (0 Beat, 1 Bar)
        Order = 8,              // FR11 playing order 0-3 (ModulationOrder)
    };
    constexpr uint8_t MOD_TARGET_FIELD_COUNT = 9;

    /**
     * @brief Post one modulation-target claim (ui-integration T7): the
     *        sequencer core claims the first free slot of its OWN
     *        ModulationRegistry and binds it to the given global modulation
     *        pattern. Single FIFO word:
     *          MOD_TARGET_ADD | patternIndex<<8
     *
     * @param patternIndex  the global modulation pattern driving the new
     *                      target (0..registry MAX_PATTERNS-1)
     */
    inline void sendModTargetAddCommand(uint8_t patternIndex) {
        sendCommand(Command::MOD_TARGET_ADD, patternIndex, 0);
    }

    /**
     * @brief Post one modulation-target field write (ui-integration T7).
     *        The (slot, field, value) triple packs into the two generic
     *        payload bytes every receive path already decodes, so the wire
     *        needs no command.cpp changes:
     *          param1 = field<<4 | slot   (16 target slots, 9 fields)
     *          param2 = value            (Balance as a signed two's-
     *                                  complement byte)
     *
     * @param targetSlot  the target slot the write addresses (0-15)
     * @param field       which ModulationTarget member the value writes
     * @param value       the new value (already UI-clamped by the view)
     */
    inline void sendModTargetSetParamCommand(uint8_t targetSlot,
                                             ModTargetField field, uint8_t value) {
        sendCommand(Command::MOD_TARGET_SET_PARAM,
                    static_cast<uint8_t>((static_cast<uint8_t>(field) << 4) |
                                         (targetSlot & 0x0Fu)),
                    value);
    }

    // Unpack the MOD_TARGET_SET_PARAM payload bytes (the receive-side mirror
    // of sendModTargetSetParamCommand's packing).
    constexpr uint8_t modTargetSlotOf(uint8_t packedParam1) {
        return static_cast<uint8_t>(packedParam1 & 0x0Fu);
    }
    constexpr uint8_t modTargetFieldOf(uint8_t packedParam1) {
        return static_cast<uint8_t>(packedParam1 >> 4);
    }

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
