#pragma once

// Minimal host stub for hardware/watchdog.h. The only watchdog call in any
// host-compiled TU is HostControl's autonomous "H:reset" power-cycle, which
// is firmware-only behavior. The real call never returns and the caller then
// spins forever waiting for the reboot, so the host double aborts rather than
// returning into that spin-loop: if a host test ever dispatches "H:reset" it
// fails loudly instead of hanging the test runner.
#include <cstdio>
#include <cstdlib>
#include <cstdint>

static inline void watchdog_reboot(uint32_t /*pc*/, uint32_t /*sp*/,
                                   uint32_t /*delay_ms*/) {
    std::fprintf(stderr, "host stub: watchdog_reboot() reached outside firmware\n");
    std::abort();
}
