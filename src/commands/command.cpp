#include <algorithm>
#include "command.h"
#include "pico/multicore.h"
#include "../sequencer/PatternSnapshot.h"

namespace commands {

    // Function to send a command to the sequencer core
    void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
        // Pack command and parameters into a single 32-bit value
        uint32_t packed = static_cast<uint32_t>(cmd) |
            (static_cast<uint32_t>(param1) << 8) |
            (static_cast<uint32_t>(param2) << 16);

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

    void sendPitchSet(uint8_t patternIndex, uint8_t count,
                      common::PlayingOrder order, const std::vector<uint8_t>& pitches) {
        const uint8_t pitchCount = static_cast<uint8_t>(
            std::min(static_cast<size_t>(count), pitches.size()));
        const uint8_t clampedCount = std::min(pitchCount, MAX_PITCH_SET_LENGTH);
        const uint32_t packed = static_cast<uint32_t>(Command::PATTERN_PITCH_SET) |
            (static_cast<uint32_t>(patternIndex) << 8) |
            (static_cast<uint32_t>(clampedCount) << 16) |
            (static_cast<uint32_t>(order) << 24);
        multicore_fifo_push_blocking(packed);
        const uint8_t wordCount = (clampedCount + 3) / 4;
        for (uint8_t wordIndex = 0; wordIndex < wordCount; wordIndex++) {
            uint32_t word = 0;
            for (uint8_t byte = 0; byte < 4; byte++) {
                const size_t pitchIndex = static_cast<size_t>(wordIndex) * 4 + byte;
                if (pitchIndex < clampedCount) {
                    word |= static_cast<uint32_t>(pitches[pitchIndex]) << (byte * 8);
                }
            }
            multicore_fifo_push_blocking(word);
        }
    }

    void sendVelocitySet(uint8_t patternIndex, uint8_t count,
                         common::PlayingOrder order,
                         const std::vector<uint8_t>& velocities) {
        const uint8_t velocityCount = static_cast<uint8_t>(
            std::min(static_cast<size_t>(count), velocities.size()));
        const uint8_t clampedCount = std::min(velocityCount, MAX_PITCH_SET_LENGTH);
        const uint32_t packed = static_cast<uint32_t>(Command::PATTERN_VELOCITY_SET) |
            (static_cast<uint32_t>(patternIndex) << 8) |
            (static_cast<uint32_t>(clampedCount) << 16) |
            (static_cast<uint32_t>(order) << 24);
        multicore_fifo_push_blocking(packed);
        const uint8_t wordCount = (clampedCount + 3) / 4;
        for (uint8_t wordIndex = 0; wordIndex < wordCount; wordIndex++) {
            uint32_t word = 0;
            for (uint8_t byte = 0; byte < 4; byte++) {
                const size_t velIndex = static_cast<size_t>(wordIndex) * 4 + byte;
                if (velIndex < clampedCount) {
                    word |= static_cast<uint32_t>(velocities[velIndex]) << (byte * 8);
                }
            }
            multicore_fifo_push_blocking(word);
        }
    }

    void sendScaleCommand(uint8_t root, uint8_t templateIndex) {
        // Global scale update: same single-word shape as BPM_SET. The receiving
        // side (ui-integration T4) applies it to the TransportTimeline, the
        // single source of truth for the global scale.
        sendCommand(Command::SCALE_SET, root, templateIndex);
    }

    void sendPatternScaleCommand(uint8_t patternIndex, uint8_t root,
                                 uint8_t templateIndex) {
        // Per-pattern scale override, packed into one FIFO word:
        //   cmd | patternIndex<<8 | root<<16 | templateIndex<<24
        const uint32_t packed = static_cast<uint32_t>(Command::PATTERN_SCALE_SET) |
            (static_cast<uint32_t>(patternIndex) << 8) |
            (static_cast<uint32_t>(root) << 16) |
            (static_cast<uint32_t>(templateIndex) << 24);
        multicore_fifo_push_blocking(packed);
    }

    void sendGenParamCommand(uint8_t patternIndex, uint8_t paramId, int16_t value) {
        // One generator parameter write (ui-integration T5): the UI's ONLY
        // cross-core write path for generator configuration. Two words —
        // the header names the (pattern, parameter) address, the second
        // word carries the 16-bit value.
        const uint32_t packed = static_cast<uint32_t>(Command::GEN_PARAM_SET) |
            (static_cast<uint32_t>(patternIndex) << 8) |
            (static_cast<uint32_t>(paramId) << 16);
        multicore_fifo_push_blocking(packed);
        multicore_fifo_push_blocking(static_cast<uint32_t>(
            static_cast<uint16_t>(value)));
    }

    void sendMutateCommand(uint8_t operatorIndex, uint8_t patternIndex,
                           uint16_t targets, uint8_t intensity, uint8_t timing) {
        // One mutation request (ui-integration T6): the MutationView's ONLY
        // cross-core path. Two words — the header names the operator by its
        // INDEX into the built-in registration order (both cores list the
        // same order independently; no registry crosses the cores) and the
        // pattern (or the ALL_PATTERNS sentinel) with the FR6 intensity in
        // the top byte; the payload word carries the FR7 target mask and
        // the packed FR10 level-1 timing override.
        const uint32_t packed = static_cast<uint32_t>(Command::MUTATE) |
            (static_cast<uint32_t>(operatorIndex) << 8) |
            (static_cast<uint32_t>(patternIndex) << 16) |
            (static_cast<uint32_t>(intensity) << 24);
        multicore_fifo_push_blocking(packed);
        multicore_fifo_push_blocking(
            static_cast<uint32_t>(targets & 0xFFFFu) |
            (static_cast<uint32_t>(timing & 0x7u) << 16));
    }

    void sendUndoCommand(uint8_t timing) { sendCommand(Command::UNDO, timing); }

    void sendRevertCommand(uint8_t timing) { sendCommand(Command::REVERT, timing); }

    void sendKeepCommand(uint8_t timing) { sendCommand(Command::KEEP, timing); }

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
            if (msg.cmd == Command::PATTERN_PITCH_SET) {
                const uint8_t receivedPitchCount = (raw_cmd >> 16) & 0xFF;
                msg.pitchCount = std::min(receivedPitchCount, MAX_PITCH_SET_LENGTH);
                msg.pitchOrder = static_cast<common::PlayingOrder>((raw_cmd >> 24) & 0xFF);
                msg.pitches.assign(msg.pitchCount, 0);
                const uint8_t wordCount = (msg.pitchCount + 3) / 4;
                for (uint8_t i = 0; i < wordCount; i++) {
                    const uint32_t word = multicore_fifo_pop_blocking();
                    for (uint8_t byte = 0; byte < 4; byte++) {
                        const size_t pitchIndex = static_cast<size_t>(i) * 4 + byte;
                        if (pitchIndex < msg.pitchCount) {
                            msg.pitches[pitchIndex] = (word >> (byte * 8)) & 0xFF;
                        }
                    }
                }
            }
            if (msg.cmd == Command::PATTERN_VELOCITY_SET) {
                const uint8_t receivedVelCount = (raw_cmd >> 16) & 0xFF;
                msg.velocityCount = std::min(receivedVelCount, MAX_PITCH_SET_LENGTH);
                msg.velocityOrder = static_cast<common::PlayingOrder>((raw_cmd >> 24) & 0xFF);
                msg.velocities.assign(msg.velocityCount, 0);
                const uint8_t wordCount = (msg.velocityCount + 3) / 4;
                for (uint8_t i = 0; i < wordCount; i++) {
                    const uint32_t word = multicore_fifo_pop_blocking();
                    for (uint8_t byte = 0; byte < 4; byte++) {
                        const size_t velIndex = static_cast<size_t>(i) * 4 + byte;
                        if (velIndex < msg.velocityCount) {
                            msg.velocities[velIndex] = (word >> (byte * 8)) & 0xFF;
                        }
                    }
                }
            }
            if (msg.cmd == Command::SCALE_SET) {
                msg.scaleRoot = msg.param1;
                msg.scaleTemplateIndex = msg.param2;
            }
            if (msg.cmd == Command::PATTERN_SCALE_SET) {
                msg.scaleRoot = msg.param2;
                msg.scaleTemplateIndex = static_cast<uint8_t>((raw_cmd >> 24) & 0xFF);
            }
            if (msg.cmd == Command::GEN_PARAM_SET) {
                // The value word follows the header (pushed by the sender).
                msg.paramValue = static_cast<int16_t>(
                    static_cast<uint16_t>(multicore_fifo_pop_blocking() & 0xFFFFu));
            }
            if (msg.cmd == Command::MUTATE) {
                // The payload word pushed by sendMutateCommand: the FR7
                // target mask plus the packed FR10 override; the FR6
                // intensity rode in the header's top byte.
                msg.mutateIntensity = static_cast<uint8_t>((raw_cmd >> 24) & 0xFFu);
                const uint32_t payload = multicore_fifo_pop_blocking();
                msg.mutateTargets = static_cast<uint16_t>(payload & 0xFFFFu);
                msg.genTiming = static_cast<uint8_t>((payload >> 16) & 0x7u);
            }
            if (msg.cmd == Command::UNDO || msg.cmd == Command::REVERT ||
                msg.cmd == Command::KEEP) {
                // The recovery commands carry the packed FR10 override in
                // the header's param1 byte.
                msg.genTiming = msg.param1;
            }
        }
        return msg;
    }

    CommandMessage receiveCommandInto(sequencer::PatternSnapshot& snapshot) {
        // Allocation-free sibling of receiveCommand() for the core1 timing path
        // (task 6.4, Req 7.9): identical FIFO decode, but multiword payloads land
        // in the reused `snapshot` instead of the heap-backed vector members. The
        // returned message carries only scalar fields; its vectors stay empty.
        commands::CommandMessage msg;
        snapshot.clear();
        if (!multicore_fifo_rvalid()) return msg;

        const uint32_t raw_cmd = multicore_fifo_pop_blocking();
        msg.cmd = static_cast<commands::Command>(raw_cmd & 0xFF);
        msg.param1 = (raw_cmd >> 8) & 0xFF;
        msg.param2 = (raw_cmd >> 16) & 0xFF;

        if (msg.cmd == Command::PATTERN_GATE_SET) {
            const uint16_t receivedGateCount = raw_cmd >> 16;
            msg.gateCount = std::min(receivedGateCount, MAX_GATE_SET_LENGTH);
            snapshot.setGateCount(msg.gateCount);
            const uint16_t wordCount = (receivedGateCount + 31) / 32;
            for (uint16_t i = 0; i < wordCount; i++) {
                const uint32_t word = multicore_fifo_pop_blocking();
                for (uint8_t bit = 0; bit < 32; bit++) {
                    const size_t gateIndex = static_cast<size_t>(i) * 32 + bit;
                    if (gateIndex < msg.gateCount) {
                        snapshot.setGate(static_cast<uint16_t>(gateIndex), word & (1u << bit));
                    }
                }
            }
        } else if (msg.cmd == Command::PATTERN_PITCH_SET) {
            const uint8_t receivedPitchCount = (raw_cmd >> 16) & 0xFF;
            msg.pitchCount = std::min(receivedPitchCount, MAX_PITCH_SET_LENGTH);
            msg.pitchOrder = static_cast<common::PlayingOrder>((raw_cmd >> 24) & 0xFF);
            snapshot.setPitchCount(msg.pitchCount);
            snapshot.setPitchOrder(msg.pitchOrder);
            const uint8_t wordCount = (msg.pitchCount + 3) / 4;
            for (uint8_t i = 0; i < wordCount; i++) {
                const uint32_t word = multicore_fifo_pop_blocking();
                for (uint8_t byte = 0; byte < 4; byte++) {
                    const size_t pitchIndex = static_cast<size_t>(i) * 4 + byte;
                    if (pitchIndex < msg.pitchCount) {
                        snapshot.setPitch(static_cast<uint8_t>(pitchIndex),
                                          (word >> (byte * 8)) & 0xFF);
                    }
                }
            }
        } else if (msg.cmd == Command::PATTERN_VELOCITY_SET) {
            const uint8_t receivedVelCount = (raw_cmd >> 16) & 0xFF;
            msg.velocityCount = std::min(receivedVelCount, MAX_PITCH_SET_LENGTH);
            msg.velocityOrder = static_cast<common::PlayingOrder>((raw_cmd >> 24) & 0xFF);
            snapshot.setVelocityCount(msg.velocityCount);
            snapshot.setVelocityOrder(msg.velocityOrder);
            const uint8_t wordCount = (msg.velocityCount + 3) / 4;
            for (uint8_t i = 0; i < wordCount; i++) {
                const uint32_t word = multicore_fifo_pop_blocking();
                for (uint8_t byte = 0; byte < 4; byte++) {
                    const size_t velIndex = static_cast<size_t>(i) * 4 + byte;
                    if (velIndex < msg.velocityCount) {
                        snapshot.setVelocity(static_cast<uint8_t>(velIndex),
                                             (word >> (byte * 8)) & 0xFF);
                    }
                }
            }
        } else if (msg.cmd == Command::SCALE_SET) {
            msg.scaleRoot = msg.param1;
            msg.scaleTemplateIndex = msg.param2;
        } else if (msg.cmd == Command::PATTERN_SCALE_SET) {
            msg.scaleRoot = msg.param2;
            msg.scaleTemplateIndex = static_cast<uint8_t>((raw_cmd >> 24) & 0xFF);
        } else if (msg.cmd == Command::GEN_PARAM_SET) {
            // The value word follows the header (pushed by the sender).
            msg.paramValue = static_cast<int16_t>(
                static_cast<uint16_t>(multicore_fifo_pop_blocking() & 0xFFFFu));
        } else if (msg.cmd == Command::MUTATE) {
            // The payload word pushed by sendMutateCommand (ui-integration
            // T6): the FR7 target mask plus the packed FR10 override; the
            // FR6 intensity rode in the header's top byte.
            msg.mutateIntensity = static_cast<uint8_t>((raw_cmd >> 24) & 0xFFu);
            const uint32_t payload = multicore_fifo_pop_blocking();
            msg.mutateTargets = static_cast<uint16_t>(payload & 0xFFFFu);
            msg.genTiming = static_cast<uint8_t>((payload >> 16) & 0x7u);
        } else if (msg.cmd == Command::UNDO || msg.cmd == Command::REVERT ||
                   msg.cmd == Command::KEEP) {
            // The recovery commands carry the packed FR10 override in the
            // header's param1 byte.
            msg.genTiming = msg.param1;
        }
        return msg;
    }

} // namespace commands
