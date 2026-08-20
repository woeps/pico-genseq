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
    testing::sentCommands().push_back({cmd, param1, param2});
}

CommandMessage receiveCommand() { return {Command::NOOP, 0, 0}; }

} // namespace commands
