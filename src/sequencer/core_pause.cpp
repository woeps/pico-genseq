#include "core_pause.h"

#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico/time.h"

namespace sequencer {

namespace {

// Set by core0 to request a pause; cleared by core0 to release. Read by core1.
volatile bool g_pauseRequested = false;

// Set by core1 once it is spinning in the RAM-resident busy-wait (i.e. it will
// not fetch instructions from flash). Read by core0 to know the pause is in
// effect. Cleared by core1 when it leaves the busy-wait.
volatile bool g_pauseAcked = false;

// Bounded wall-clock timeout for core1 to acknowledge the release so a new
// request cannot race an in-progress spin. The ACK wait itself uses the
// caller-supplied absolute-time timeout (task 9.1).
constexpr uint32_t RELEASE_WAIT_MS = 50u;

// core1's busy-wait. MUST live in RAM: while core0 erases/programs flash the XIP
// interface is down, so any instruction fetched from flash would fault. Marked
// __not_in_flash_func so this routine (and its tight loop) executes from RAM.
void __not_in_flash_func(corePauseSpin)() {
    // Announce we are parked in RAM, then hold with interrupts disabled until
    // core0 clears the request. Disabling interrupts guarantees no flash-resident
    // ISR runs on core1 during the write window.
    const uint32_t irq = save_and_disable_interrupts();
    g_pauseAcked = true;
    while (g_pauseRequested) {
        tight_loop_contents();
    }
    g_pauseAcked = false;
    restore_interrupts(irq);
}

}  // namespace

void __not_in_flash_func(corePauseCheck)() {
    if (g_pauseRequested) {
        corePauseSpin();
    }
}

bool corePauseRequestAndWait(uint32_t timeoutMs) {
    g_pauseAcked = false;
    g_pauseRequested = true;
    // Wait for a POSITIVE acknowledgement using an ABSOLUTE-TIME deadline
    // (task 9.1, Req 8.8/9.8): deterministic regardless of core clock. On
    // timeout we return false WITHOUT having a confirmed pause, so the caller
    // must abort the flash write (no destructive erase/program).
    const absolute_time_t deadline = make_timeout_time_ms(timeoutMs);
    while (!g_pauseAcked) {
        if (time_reached(deadline)) {
            // No positive ack within the deadline: clear the request so core1
            // (if it wakes late) does not spin forever, and report failure.
            g_pauseRequested = false;
            return false;
        }
        tight_loop_contents();
    }
    return true;  // positive acknowledgement — safe to proceed with the write
}

void corePauseRelease() {
    g_pauseRequested = false;
    // Wait (bounded, absolute-time) for core1 to observe the release and leave
    // its busy-wait, so a subsequent request cannot race an in-progress spin.
    const absolute_time_t deadline = make_timeout_time_ms(RELEASE_WAIT_MS);
    while (g_pauseAcked && !time_reached(deadline)) {
        tight_loop_contents();
    }
}

}  // namespace sequencer
