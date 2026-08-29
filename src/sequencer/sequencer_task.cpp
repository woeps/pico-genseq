#include "sequencer.h"
#include "UartMidiOutput.h"
#include "pico/multicore.h"
#include "core_pause.h"
#include "../commands/command.h"

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
            while (true) {
                // Cooperative pause point: if core0 is about to write flash it
                // sets the pause request; we acknowledge and spin in RAM (IRQs
                // off) until released, so this core never fetches from flash
                // (XIP) mid-erase/program (Req 4.4, 4.5).
                corePauseCheck();

                commands::CommandMessage msg = commands::receiveCommand();
                globalSequencer->processCommand(msg);
                globalSequencer->update();
            }
        }
    }

    void createSequencerTask(uart_inst_t* uart, uint txPin, uint rxPin) {
        // Transport constructed first; its lifetime encloses the Sequencer's (Req 4.3).
        static UartMidiOutput midiOutput(uart, txPin, rxPin);
        static Sequencer sequencer(midiOutput);
        static bool launched = false;

        globalSequencer = &sequencer;
        globalSequencer->init();

        if (!launched) {
            launched = true;
            multicore_launch_core1(sequencer_task);
        }
    }

} // namespace sequencer
