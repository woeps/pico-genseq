#pragma once

#include <cstdint>

// Cooperative cross-core pause for safe flash writes (persist-settings).
//
// Background: the inter-core command channel (commands::) uses the RP2040 SIO
// FIFO. The Pico SDK's multicore_lockout_* mechanism ALSO commandeers that same
// FIFO for its handshake, so the two cannot coexist - using multicore lockout
// silently swallows command words (PLAY/STOP/etc.) and breaks the sequencer.
//
// Instead, core0 asks core1 to pause cooperatively: core1's main loop polls a
// request flag each iteration and, when asked, acknowledges and spins in a
// RAM-resident busy-wait with interrupts disabled until core0 releases it. This
// keeps core1 from executing code out of flash (XIP) during an erase/program,
// exactly like lockout did, but leaves the command FIFO untouched.
//
// Usage:
//   core1 (sequencer loop):  call corePauseCheck() once per iteration.
//   core0 (flash writer):    corePauseRequestAndWait();  ... write flash ...
//                            corePauseRelease();

namespace sequencer {

// Called by core1 in its main loop. If core0 has requested a pause, this
// acknowledges it and busy-waits (from RAM, interrupts disabled) until core0
// releases the pause. Returns immediately when no pause is pending. Safe to call
// every loop iteration.
void corePauseCheck();

// Called by core0. Requests that core1 pause and blocks until core1 has
// acknowledged (i.e. is spinning in RAM and will not fetch from flash). After
// this returns it is safe to erase/program flash. MUST be paired with
// corePauseRelease().
//
// If core1 has not been started/observed yet (no acknowledgement within a bounded
// spin), this still returns so a save cannot hang forever; callers disable
// interrupts around the actual flash op regardless.
void corePauseRequestAndWait();

// Called by core0 after the flash operation completes. Releases core1 from its
// busy-wait so it resumes the normal command/update loop.
void corePauseRelease();

}  // namespace sequencer
