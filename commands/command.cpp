#include "command.h"
#include "pico/multicore.h"

namespace commands {

// Function to send a command to the sequencer core
void sendCommand(Command cmd, uint32_t param1, uint32_t param2) {
    // Pack command and parameters into a single 32-bit value
    uint32_t packed = static_cast<uint32_t>(cmd) | 
                      ((param1 & 0xFFFF) << 8) | 
                      ((param2 & 0xFF) << 24);
    
    // Send to the sequencer core
    multicore_fifo_push_blocking(packed);
}

} // namespace commands
