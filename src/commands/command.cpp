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

    void sendGenParams(uint8_t patternIndex, const common::GenerationParameters& p) {
        // Header: cmd | patternIndex<<8.
        const uint32_t header = static_cast<uint32_t>(Command::GEN_PARAMS_SET) |
            (static_cast<uint32_t>(patternIndex) << 8);
        multicore_fifo_push_blocking(header);

        // Word 1: scale | scaleRoot | quantize | patternProbability.
        multicore_fifo_push_blocking(
            static_cast<uint32_t>(p.scale) |
            (static_cast<uint32_t>(p.scaleRoot) << 8) |
            (static_cast<uint32_t>(p.quantize) << 16) |
            (static_cast<uint32_t>(p.patternProbability) << 24));
        // Word 2: humanizeRange | seedSet | coupling | stepLength.
        multicore_fifo_push_blocking(
            static_cast<uint32_t>(p.humanizeRange) |
            (static_cast<uint32_t>(p.seedSet ? 1u : 0u) << 8) |
            (static_cast<uint32_t>(p.coupling) << 16) |
            (static_cast<uint32_t>(p.stepLength) << 24));
        // Word 3: seed (full 32 bits).
        multicore_fifo_push_blocking(p.seed);
        // Words 4..7: the 16 per-step probability bytes, 4 per word.
        for (uint8_t w = 0; w < 4; ++w) {
            uint32_t word = 0;
            for (uint8_t b = 0; b < 4; ++b) {
                word |= static_cast<uint32_t>(p.stepProbability[w * 4 + b]) << (b * 8);
            }
            multicore_fifo_push_blocking(word);
        }
    }

    namespace {
    // Decode the 6 payload words (after the header) of a GEN_PARAMS_SET command
    // into a GenerationParameters. Shared by receiveCommand / receiveCommandInto.
    common::GenerationParameters decodeGenParamsPayload() {
        common::GenerationParameters p;
        const uint32_t w1 = multicore_fifo_pop_blocking();
        p.scale = static_cast<common::ScaleId>(w1 & 0xFF);
        p.scaleRoot = (w1 >> 8) & 0xFF;
        p.quantize = static_cast<common::QuantizeMode>((w1 >> 16) & 0xFF);
        p.patternProbability = (w1 >> 24) & 0xFF;
        const uint32_t w2 = multicore_fifo_pop_blocking();
        p.humanizeRange = w2 & 0xFF;
        p.seedSet = ((w2 >> 8) & 0xFF) != 0;
        p.coupling = static_cast<common::CouplingMode>((w2 >> 16) & 0xFF);
        p.stepLength = (w2 >> 24) & 0xFF;
        p.seed = multicore_fifo_pop_blocking();
        for (uint8_t w = 0; w < 4; ++w) {
            const uint32_t word = multicore_fifo_pop_blocking();
            for (uint8_t b = 0; b < 4; ++b) {
                p.stepProbability[w * 4 + b] = (word >> (b * 8)) & 0xFF;
            }
        }
        return p;
    }
    }  // namespace

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
            if (msg.cmd == Command::GEN_PARAMS_SET) {
                // Drain the fixed 6-word payload so the FIFO stays aligned. The
                // heap CommandMessage has no gen-params field; the engine applies
                // gen-params via the snapshot path (receiveCommandInto), so here
                // we only consume the words.
                (void)decodeGenParamsPayload();
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
        } else if (msg.cmd == Command::GEN_PARAMS_SET) {
            // Fixed 6-word payload decoded into the snapshot's GenerationParameters
            // (task 10.4). Allocation-free: a POD copy into the reused snapshot.
            snapshot.setGenerationParameters(decodeGenParamsPayload());
        }
        return msg;
    }

} // namespace commands
