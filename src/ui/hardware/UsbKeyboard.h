#pragma once

#include <cstdint>
#include "KeyboardDecoder.h"

namespace hardware {

// TinyUSB host adapter. Owns the decoder, supplies it with reports and a
// millisecond clock, and dispatches decoded events into the StateManager.
class UsbKeyboard {
public:
    UsbKeyboard();

    void initialize();
    void update();      // pumps tuh_task() and the decoder's repeat timer

    // Entry points for the TinyUSB C callbacks.
    static UsbKeyboard* instance();
    void handleReport(const uint8_t* report, uint16_t len);
    void handleDisconnect();

private:
    KeyboardDecoder decoder;
};

} // namespace hardware
