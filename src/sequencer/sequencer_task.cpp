#include "sequencer.h"
#include "PicoMonotonicClock.h"
#include "UartMidiOutput.h"
#include "PatternSnapshot.h"
#include "pico/multicore.h"
#include "core_pause.h"
#include "../commands/command.h"
#include "../instrumentation/PerformanceTrace.h"

// Temporary core1 boot-fault diagnostic (BUG B). Off by default; enable with
// -DGENSEQ_CORE1_BOOT_TRACE=ON. Never ship enabled.
#ifndef GENSEQ_CORE1_BOOT_TRACE
#define GENSEQ_CORE1_BOOT_TRACE 0
#endif

#if GENSEQ_CORE1_BOOT_TRACE
#include "hardware/exception.h"
#include "hardware/structs/scb.h"
#endif

namespace sequencer {

// Global pointer for multicore communication (moved from sequencer.cpp)
static Sequencer* globalSequencer = nullptr;

// Reused fixed-capacity decode buffer (task 6.4, Req 7.9). Hoisted from a
// FUNCTION-LOCAL static inside sequencer_task() to FILE SCOPE so its
// construction is a zero-init in BSS at image load — NOT a lazy, __cxa_guard-
// protected first-use construction that runs ON CORE1. The guarded local-static
// init path is a BUG B suspect: the C++ ABI guard acquire/release and the
// guarded zeroing run on core1 during its very first loop entry, and the guard
// machinery is not obviously safe on this core/runtime. A file-scope POD has no
// guard and no runtime constructor, so core1 never executes that path.
static sequencer::PatternSnapshot g_commandSnapshot;

#if GENSEQ_CORE1_BOOT_TRACE
// ---------------------------------------------------------------------------
// Core1 boot-fault breadcrumb (BUG B diagnosis, temporary — GENSEQ_CORE1_BOOT_TRACE).
// SWD reads these after a self-reset to see exactly how far core1 got and, if it
// faulted, the true faulting PC captured by a core1-local HardFault handler
// (the hardware exception frame is unreadable once core1 double-faults, so we
// snapshot the stacked PC ourselves on the FIRST fault). All in BSS so the
// addresses are stable and readable over SWD even after lockup.
// Stage codes: 1=task entry, 2=loop top, 3=after receiveCommandInto,
// 4=before processCommand, 5=after processCommand, 6=after update.
// g_core1LastCmd = the cmd enum of the command currently being applied.
// g_core1Fault* = set by the HardFault handler.
// ---------------------------------------------------------------------------
volatile uint32_t g_core1Stage      = 0;
volatile uint32_t g_core1LastCmd    = 0xFF;
volatile uint32_t g_core1LoopCount  = 0;
volatile uint32_t g_core1FaultHit    = 0;
volatile uint32_t g_core1FaultPC     = 0;
volatile uint32_t g_core1FaultLR     = 0;
volatile uint32_t g_core1FaultR0     = 0;
volatile uint32_t g_core1FaultSP     = 0;
// Idle-path field snapshots (written by Sequencer::update under the same flag).
volatile uint32_t g_core1UpdateN     = 0;
volatile uint32_t g_core1Playing     = 0;
volatile uint32_t g_core1OutputVtbl  = 0;
volatile uint32_t g_core1ClockVtbl   = 0;
// BUG B part 2: command-apply breadcrumbs (written by Sequencer::processCommand).
volatile uint32_t g_core1LastRealCmd = 0xFF;
volatile uint32_t g_core1ApplyBranch = 0;
volatile uint32_t g_core1ApplyCount  = 0;
volatile uint32_t g_core1ApplyParam  = 0;

// BUG B part 2: FINE-GRAINED "PC-class" region code. core1 writes g_core1Region
// at many points through its idle loop; the HardFault handler FREEZES the value
// into g_core1FaultRegion so the region core1 was executing AT the fault instant
// survives the double-fault lockup (the stacked PC does not). g_core1RegionMax =
// furthest region reached. Region codes (see BB_REGION below + callees):
//  10 loop-top  11 pre-receive  12 post-receive  13 pre-processCmd
//  14 post-processCmd  15 pre-update  16 post-update (loop bottom)
//  20 receiveCommandInto entry (pre clear/ROM-memset)  21 post clear()  22 post rvalid-check
//  30 update() entry  31 update() after soundingCount() (pre !playing return)
volatile uint32_t g_core1Region      = 0;
volatile uint32_t g_core1FaultRegion = 0xFFFFFFFFu;
volatile uint32_t g_core1RegionMax   = 0;

// C worker: given the exact exception-frame SP, snapshot R0 and the stacked PC.
extern "C" __attribute__((used)) void core1BootTraceFaultCapture(uint32_t sp, uint32_t lr) {
    g_core1FaultRegion = g_core1Region;  // FREEZE the region at the fault instant
    const uint32_t* frame = reinterpret_cast<const uint32_t*>(sp);
    g_core1FaultLR = lr;
    g_core1FaultSP = sp;
    // Stacked frame: R0 R1 R2 R3 R12 LR PC xPSR. Guard against a wild SP.
    if (sp >= 0x20000000 && sp < (0x20042000 - 32)) {
        g_core1FaultR0 = frame[0];
        g_core1FaultPC = frame[6];
    } else {
        g_core1FaultPC = 0xBADF0000u | (sp & 0xFFFFu);
    }
    g_core1FaultHit = 0xFA017ED0u;  // "FAULTED" marker
#if GENSEQ_TRACE_FAULT_PIN >= 0
    instrumentation::PerformanceTrace::setFault(true);
#endif
    while (true) { __asm volatile("wfe"); }
}

// Naked HardFault entry: no prologue, so we read the UNMODIFIED exception frame
// SP (MSP or PSP per EXC_RETURN bit 2 in LR) and hand it to the C worker.
extern "C" __attribute__((naked, used)) void core1BootTraceHardFault() {
    __asm volatile(
        "movs r0, #4        \n"
        "mov  r1, lr        \n"
        "tst  r0, r1        \n"   // EXC_RETURN bit2: 0 => MSP, !=0 => PSP
        "beq  1f            \n"
        "mrs  r0, psp       \n"
        "b    2f            \n"
        "1:                 \n"
        "mrs  r0, msp       \n"
        "2:                 \n"
        "mov  r1, lr        \n"   // r0=frame SP, r1=EXC_RETURN
        "ldr  r2, =core1BootTraceFaultCapture \n"
        "bx   r2            \n"
    );
}
#endif  // GENSEQ_CORE1_BOOT_TRACE

#if GENSEQ_CORE1_BOOT_TRACE
#define BB_REGION(n) do { g_core1Region = (n); if ((n) > g_core1RegionMax) g_core1RegionMax = (n); } while (0)
#else
#define BB_REGION(n) do {} while (0)
#endif

// Sequencer task for the second core
static void sequencer_task() {
    // NOTE: we deliberately do NOT use multicore_lockout_victim_init() here.
    // The SDK's multicore lockout commandeers the SIO inter-core FIFO for its
    // handshake, but that same FIFO carries our commands:: messages
    // (PLAY/STOP/BPM/pattern edits). Combining the two makes the lockout IRQ
    // swallow command words, so the sequencer never sees PLAY and MIDI stops.
    // Instead core0 pauses this core cooperatively for flash writes via
    // corePauseCheck() below (see core_pause.h), leaving the FIFO untouched.
#if GENSEQ_CORE1_BOOT_TRACE
    // Install core1-local HardFault handler FIRST so the very earliest fault is
    // captured with the true stacked PC (exception_set_exclusive_handler sets
    // THIS core's VTOR handler; core1 has its own exception table slot).
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, core1BootTraceHardFault);
    g_core1Stage = 1;  // reached task entry
#endif
    if (globalSequencer) {
        // Decode buffer is the file-scope g_commandSnapshot (see above): no
        // function-local-static guard runs on core1.
        sequencer::PatternSnapshot& commandSnapshot = g_commandSnapshot;
        while (true) {
            // Cooperative pause point: if core0 is about to write flash it
            // sets the pause request; we acknowledge and spin in RAM (IRQs
            // off) until released, so this core never fetches from flash
            // (XIP) mid-erase/program (Req 4.4, 4.5).
            corePauseCheck();
            BB_REGION(10);

#if GENSEQ_CORE1_BOOT_TRACE
            g_core1Stage = 2; ++g_core1LoopCount;
#endif
            BB_REGION(11);
            commands::CommandMessage msg = commands::receiveCommandInto(commandSnapshot);
            BB_REGION(12);
#if GENSEQ_CORE1_BOOT_TRACE
            g_core1Stage = 3; g_core1LastCmd = static_cast<uint32_t>(msg.cmd);
            g_core1Stage = 4;
#endif
            BB_REGION(13);
            globalSequencer->processCommand(msg, commandSnapshot);
            BB_REGION(14);
#if GENSEQ_CORE1_BOOT_TRACE
            g_core1Stage = 5;
#endif
            BB_REGION(15);
            globalSequencer->update();
            BB_REGION(16);
#if GENSEQ_CORE1_BOOT_TRACE
            g_core1Stage = 6;
#endif
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
