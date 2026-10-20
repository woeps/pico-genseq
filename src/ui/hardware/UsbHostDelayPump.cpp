#include "pico/stdlib.h"
#include "pico/time.h"
#include "../UiPump.h"

// Keep the UI/input loop responsive DURING TinyUSB's blocking host delays
// (task 12.3). TinyUSB's host enumeration spins in tusb_time_delay_ms_api()
// for up to ENUM_DEBOUNCING_DELAY_MS + ENUM_RESET_DELAY_MS = 500 ms, called
// inline from tuh_task() on the core0 UI loop. A composite/wireless receiver
// (e.g. a Logitech Unifying dongle) can re-trigger this periodically, which
// otherwise blows the Req 8.7 <=50 ms UI/input service-gap budget.
//
// tusb_time_delay_ms_api is TU_ATTR_WEAK, so we override it: instead of a bare
// busy-wait, we pump a registered UI-render callback while the delay elapses.
// The full wall-clock delay TinyUSB asked for still passes (enumeration timing
// is preserved), but the UI keeps rendering throughout, so the service gap
// stays small. Reentrancy-safe: the pump does render-only work and never calls
// back into tuh_task(), and this runs only on core0 (the UI core).

namespace ui {

// Registered by UIController::initialize(). Renders one UI frame; must NOT call
// tuh_task() / keyboard->update() (that is what invoked us).
static void (*g_ui_pump)() = nullptr;

void setUiPump(void (*pump)()) { g_ui_pump = pump; }

}  // namespace ui

// C linkage: overrides TinyUSB's weak default (src/tusb.c).
extern "C" void tusb_time_delay_ms_api(uint32_t ms) {
    const absolute_time_t deadline = make_timeout_time_ms(ms);
    while (!time_reached(deadline)) {
        if (ui::g_ui_pump) {
            ui::g_ui_pump();   // keep the UI/input loop serviced during the wait
        }
        tight_loop_contents();
    }
}
