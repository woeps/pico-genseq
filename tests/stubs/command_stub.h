#pragma once

#include <vector>
#include "commands/command.h"

namespace commands::testing {

std::vector<CommandMessage>& sentCommands();
void reset();

} // namespace commands::testing
