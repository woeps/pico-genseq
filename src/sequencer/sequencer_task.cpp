#include "sequencer.h"
#include "PicoMonotonicClock.h"
#include "UartMidiOutput.h"
#include "PatternSnapshot.h"
#include "pico/multicore.h"
#include "core_pause.h"
#include "../commands/command.h"
#include "../instrumentation/PerformanceTrace.h"

namespace sequencer {

// Global pointer for multicore communication (moved from sequencer.cpp)
static Sequencer* globalSequencer = nullptr;

// Sequencer task for the second core
static void sequencer_task() {
    // NOTE: we deliberately do NOT use multicore_lockout_victim_init() here.
    // The SDK's multicore lockout commandeers the SIO inter-core FIFO for its
    // handshake, but that same FIFO carries our commands:: messages
    // (PLAY/STOP/BPM/pattern edits). Combining the two makes the lockout IRQ
    // swallow command words, so the sequencer never sees PLAY and MIDI stops.
    // Instead core0 pauses this core cooperatively for flash writes via
    // corePauseCheck() below (see core_pause.h), leaving the FIFO untouched.
    if (globalSequencer) {
        // Reused fixed-capacity decode buffer (task 6.4, Req 7.9): allocated once
        // here, never on the per-command path. receiveCommandInto() decodes each
        // command's payload into this snapshot in place, so core1's steady-state
        // receive loop performs no heap allocation.
        static sequencer::PatternSnapshot commandSnapshot;
        while (true) {
            // Cooperative pause point: if core0 is about to write flash it
            // sets the pause request; we acknowledge and spin in RAM (IRQs
            // off) until released, so this core never fetches from flash
            // (XIP) mid-erase/program (Req 4.4, 4.5).
            corePauseCheck();

            commands::CommandMessage msg = commands::receiveCommandInto(commandSnapshot);
            globalSequencer->processCommand(msg, commandSnapshot);
            globalSequencer->update();
        }
    }
}

void createSequencerTask(uart_inst_t* uart, uint txPin, uint rxPin) {
    // Dependencies are constructed before Sequencer so their lifetimes enclose
    // every core1 access to the injected references.
    static UartMidiOutput midiOutput(uart, txPin, rxPin);
    static PicoMonotonicClock monotonicClock;
    static Sequencer sequencer(midiOutput, monotonicClock);
    static bool launched = false;

    globalSequencer = &sequencer;
    globalSequencer->init();

    if (!launched) {
        // Initialize traces exactly once immediately before launching core1.
        // Repeated factory calls must not drive live trace pins back to zero.
        instrumentation::PerformanceTrace::initialize();
        launched = true;
        multicore_launch_core1(sequencer_task);
    }
}

}  // namespace sequencer
