#pragma once

#include <cstddef>
#include <vector>
#include "commands/command.h"

namespace commands::testing {

std::vector<CommandMessage>& sentCommands();
void reset();
size_t countOf(Command cmd);

} // namespace commands::testing
