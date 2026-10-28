#pragma once

// Minimal host stub for the pico/stdlib.h surface used by firmware adapters
// compiled host-side (HostControl.cpp, via test/unit/test_host_control_view_names.cpp).
// Mirrors the SDK contract for the few functions those TUs reference: a
// non-blocking stdin read that reports "nothing pending" as
// PICO_ERROR_TIMEOUT (-1), the idle-loop hint, and the stdio flush issued
// before a watchdog reboot. Everything else resolves through the other
// pico_stubs headers (pico/time.h) or the C library.
#include <cstdint>

static inline int getchar_timeout_us(uint32_t /*timeout_us*/) {
    return -1;  // PICO_ERROR_TIMEOUT: no host stdin traffic in unit tests.
}

static inline void tight_loop_contents() {}

static inline void stdio_flush() {}
