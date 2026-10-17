// Feature: midi-output-interface, Example test: exactly-once core1 launch guard
//
// Validates: Requirements 4.4.
//
// Requirement 4.4: repeated invocation of createSequencerTask SHALL launch core1 exactly once
// and SHALL NOT construct a second Sequencer / UartMidiOutput.
//
// This is an EXAMPLE test (not property-based). It compiles the REAL
// src/sequencer/sequencer_task.cpp host-side against counting seams so we can observe:
//   - how many times multicore_launch_core1 is invoked  (g_launchCount)
//   - how many times UartMidiOutput is constructed       (g_uartCtorCount)
//
// The real sequencer_task.cpp declares the transport and sequencer as function-local
// `static`s and guards the launch with a `static bool launched`. Calling
// createSequencerTask multiple times must therefore:
//   - construct UartMidiOutput (and Sequencer) exactly once (function-local static init), and
//   - call multicore_launch_core1 exactly once (the `launched` guard).
//
// Host-side wiring: sequencer_task.cpp includes the real UartMidiOutput.h and pico/multicore.h,
// and references commands::receiveCommand. To compile/link host-side WITHOUT the Pico SDK, this
// file provides the definitions of those seams:
//   - multicore_launch_core1: counts the call and returns (it does NOT run sequencer_task, so
//     there is no infinite loop and receiveCommand is never actually executed).
//   - UartMidiOutput ctor/write: host-safe, ctor counts, write is a no-op.
//   - commands::receiveCommand: a linker seam only (sequencer_task is never executed), returns
//     a default-constructed message.
//
// The real UartMidiOutput.cpp is NOT compiled into this target (and not part of genseq_emission),
// so the definitions below are the only ones — no duplicate symbols. globalSequencer and
// sequencer_task inside sequencer_task.cpp have internal linkage, so they don't collide.

#include "sequencer.h"          // sequencer::createSequencerTask declaration
#include "UartMidiOutput.h"     // sequencer::UartMidiOutput declaration (real header)
#include "pico/multicore.h"     // multicore_launch_core1 declaration (host stub)
#include "../commands/command.h"

#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------------------
// Counting seams (definitions the real sequencer_task.cpp links against).
// ---------------------------------------------------------------------------

// Number of times multicore_launch_core1 was invoked across all createSequencerTask calls.
static int g_launchCount = 0;

// Number of times a UartMidiOutput was constructed.
static int g_uartCtorCount = 0;

// Counting definition of the multicore launch. Signature must match the host stub declaration
// in test/pico_stubs/pico/multicore.h and be compatible with the call site in
// sequencer_task.cpp: multicore_launch_core1(sequencer_task) where
// sequencer_task is `static void sequencer_task()`.
//
// Deliberately does NOT invoke `entry` — it only records the call and returns, so no infinite
// loop runs and commands::receiveCommand is never actually executed.
void multicore_launch_core1(void (*entry)(void)) {
    (void)entry;
    ++g_launchCount;
}

namespace sequencer {

// Host-safe UartMidiOutput definitions matching the real header's declared signatures.
// The ctor counts constructions instead of touching UART/GPIO hardware; write is a no-op.
UartMidiOutput::UartMidiOutput(uart_inst_t* uartArg, uint /*txPin*/, uint /*rxPin*/) {
    uart = uartArg;
    ++g_uartCtorCount;
}

void UartMidiOutput::write(uint8_t /*byte*/) {
    // No-op host double.
}

// Linker seam only: sequencer_task() now calls corePauseCheck() once per loop
// iteration (cooperative flash-write pause). The loop never runs in this test
// (multicore_launch_core1 above does not invoke the entry function), so a no-op
// host definition is all that is needed to satisfy the linker. The real
// implementation lives in src/sequencer/core_pause.cpp (SDK-dependent) and is
// intentionally not compiled host-side.
void corePauseCheck() {
    // No-op host double.
}

}  // namespace sequencer

namespace commands {

// Linker seam only: sequencer_task (which calls this) is never executed in this test because
// the counting multicore_launch_core1 above does not invoke the entry function.
CommandMessage receiveCommand() {
    return CommandMessage{};
}

}  // namespace commands

// ---------------------------------------------------------------------------
// The example test.
// ---------------------------------------------------------------------------

int main() {
    // Drive the launch path multiple times. Req 4.4: exactly-once launch, no second construction.
    sequencer::createSequencerTask(nullptr, 0, 1);
    sequencer::createSequencerTask(nullptr, 0, 1);
    sequencer::createSequencerTask(nullptr, 0, 1);

    bool ok = true;

    if (g_launchCount != 1) {
        std::fprintf(stderr,
                     "FAIL: multicore_launch_core1 called %d times; expected exactly 1 "
                     "(launch guard broken)\n",
                     g_launchCount);
        ok = false;
    }

    if (g_uartCtorCount != 1) {
        std::fprintf(stderr,
                     "FAIL: UartMidiOutput constructed %d times; expected exactly 1 "
                     "(function-local static should construct once -> no second "
                     "UartMidiOutput/Sequencer)\n",
                     g_uartCtorCount);
        ok = false;
    }

    if (!ok) {
        return 1;
    }

    std::printf("PASS: core1 launched exactly once (%d) and UartMidiOutput constructed exactly "
                "once (%d) across repeated createSequencerTask calls\n",
                g_launchCount, g_uartCtorCount);
    return 0;
}
