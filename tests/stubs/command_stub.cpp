#include <algorithm>
#include "commands/command.h"
#include "command_stub.h"

namespace commands {

namespace testing {
std::vector<CommandMessage>& sentCommands() {
    static std::vector<CommandMessage> v;
    return v;
}
void reset() { sentCommands().clear(); }
size_t countOf(Command cmd) {
    size_t n = 0;
    for (const auto& m : sentCommands()) if (m.cmd == cmd) ++n;
    return n;
}
} // namespace testing

void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
    CommandMessage msg;
    msg.cmd = cmd;
    msg.param1 = param1;
    msg.param2 = param2;
    testing::sentCommands().push_back(msg);
}

void sendBatchBegin(uint8_t commandCount) { sendCommand(Command::BATCH_BEGIN, commandCount); }
void sendBatchEnd() { sendCommand(Command::BATCH_END); }

void sendGateSet(uint8_t patternIndex, const std::vector<bool>& gates) {
    CommandMessage msg;
    msg.cmd = Command::PATTERN_GATE_SET;
    msg.param1 = patternIndex;
    msg.gateCount = static_cast<uint16_t>(
        std::min(gates.size(), static_cast<size_t>(MAX_GATE_SET_LENGTH)));
    msg.gates.assign(gates.begin(), gates.begin() + msg.gateCount);
    testing::sentCommands().push_back(msg);
}

void sendPitchSet(uint8_t patternIndex, uint8_t count,
                  common::PlayingOrder order, const std::vector<uint8_t>& pitches) {
    CommandMessage msg;
    msg.cmd = Command::PATTERN_PITCH_SET;
    msg.param1 = patternIndex;
    msg.pitchCount = static_cast<uint8_t>(
        std::min(static_cast<size_t>(count), pitches.size()));
    msg.pitchCount = std::min(msg.pitchCount, MAX_PITCH_SET_LENGTH);
    msg.pitchOrder = order;
    msg.pitches.assign(pitches.begin(), pitches.begin() + msg.pitchCount);
    testing::sentCommands().push_back(msg);
}

void sendVelocitySet(uint8_t patternIndex, uint8_t count,
                     common::PlayingOrder order, const std::vector<uint8_t>& velocities) {
    CommandMessage msg;
    msg.cmd = Command::PATTERN_VELOCITY_SET;
    msg.param1 = patternIndex;
    msg.velocityCount = static_cast<uint8_t>(
        std::min(static_cast<size_t>(count), velocities.size()));
    msg.velocityCount = std::min(msg.velocityCount, MAX_PITCH_SET_LENGTH);
    msg.velocityOrder = order;
    msg.velocities.assign(velocities.begin(), velocities.begin() + msg.velocityCount);
    testing::sentCommands().push_back(msg);
}

void sendGenParams(uint8_t patternIndex, const common::GenerationParameters&) {
    CommandMessage msg;
    msg.cmd = Command::GEN_PARAMS_SET;
    msg.param1 = patternIndex;
    testing::sentCommands().push_back(msg);
}

CommandMessage receiveCommand() { return {}; }

} // namespace commands
