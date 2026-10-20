#pragma once

// Register a render-only UI pump that TinyUSB's host-delay override calls while
// it busy-waits, keeping the UI/input loop serviced during a USB enumeration
// delay (task 12.3). See UsbHostDelayPump.cpp. The callback must do render-only
// work and must NOT call tuh_task()/keyboard->update() (no reentrancy).

namespace ui {
void setUiPump(void (*pump)());
}  // namespace ui
