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
} // namespace testing

void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
    CommandMessage msg;
    msg.cmd = cmd;
    msg.param1 = param1;
    msg.param2 = param2;
    testing::sentCommands().push_back(msg);
}

void sendGateSet(uint8_t patternIndex, const std::vector<bool>& gates) {
    CommandMessage msg;
    msg.cmd = Command::PATTERN_GATE_SET;
    msg.param1 = patternIndex;
    msg.gateCount = static_cast<uint16_t>(
        std::min(gates.size(), static_cast<size_t>(MAX_GATE_SET_LENGTH)));
    msg.gates.assign(gates.begin(), gates.begin() + msg.gateCount);
    testing::sentCommands().push_back(msg);
}

CommandMessage receiveCommand() { return {}; }

} // namespace commands
