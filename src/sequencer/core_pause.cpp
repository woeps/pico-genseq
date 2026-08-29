#include "core_pause.h"

#include "hardware/sync.h"
#include "pico/platform.h"

namespace sequencer {

namespace {

// Set by core0 to request a pause; cleared by core0 to release. Read by core1.
volatile bool g_pauseRequested = false;

// Set by core1 once it is spinning in the RAM-resident busy-wait (i.e. it will
// not fetch instructions from flash). Read by core0 to know the pause is in
// effect. Cleared by core1 when it leaves the busy-wait.
volatile bool g_pauseAcked = false;

// Bounded spin so core0 never hangs forever waiting for an acknowledgement that
// cannot come (e.g. core1 not running yet). Generous relative to a flash sector
// erase+program (a few ms); the loop below is a simple decrementing counter, not
// wall-clock, so this is just an upper safety bound.
constexpr uint32_t ACK_WAIT_SPINS = 50u * 1000u * 1000u;

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

void corePauseRequestAndWait() {
    g_pauseAcked = false;
    g_pauseRequested = true;
    // Wait (bounded) for core1 to enter its RAM busy-wait and acknowledge.
    for (uint32_t i = 0; i < ACK_WAIT_SPINS && !g_pauseAcked; ++i) {
        tight_loop_contents();
    }
}

void corePauseRelease() {
    g_pauseRequested = false;
    // Wait (bounded) for core1 to observe the release and leave its busy-wait,
    // so a subsequent request cannot race an in-progress spin.
    for (uint32_t i = 0; i < ACK_WAIT_SPINS && g_pauseAcked; ++i) {
        tight_loop_contents();
    }
}

}  // namespace sequencer
