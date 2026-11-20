#pragma once

#include <cstdint>

// Dual-core stack-overflow guard (BUG A / BUG B-part-2 class).
//
// WHY: twice now a large (~2 KB) by-value local on core0's 2 KB stack
// (UIController::initialize's UIState/PersistableConfig; RecordStore::
// selectNewest's PersistableConfig probe) overflowed core0's stack DOWN past
// __StackBottom and across the inter-stack gap into CORE1's adjacent stack,
// silently corrupting core1's live stack -> core1 took an asynchronous
// HardFault with an UNSTACKABLE frame (its own stack was stomped), which was
// brutal to diagnose.
//
// This guard makes that class LOUD instead of silent and CHEAP enough to ship.
// It exploits the linker layout: between core1's stack and core0's stack there
// is an UNUSED inter-stack gap. Neither stack touches the gap in normal
// operation, so a sentinel placed there has ZERO false positives -- the ONLY
// way it changes is a stack overrunning its region into the gap. Core0's stack
// grows DOWN toward the gap, so a core0 overflow hits the gap FIRST, before it
// ever reaches core1 -- i.e. this catches the hazard one step EARLY.
//
// RP2040 layout (this firmware's linker script):
//   core1 stack : [__StackOneBottom .. __StackOneTop)   grows down
//   GAP         : [__StackOneTop    .. __StackBottom )   <-- UNUSED, canaries here
//   core0 stack : [__StackBottom    .. __StackTop    )   grows down
//
// Runs on BOTH cores from RAM (no flash/XIP, heap, or libc), safe on the core1
// timing path and during boot. All inline, no .cpp needed.
//
// FIRMWARE-ONLY: the canaries live at linker-provided stack-boundary symbols
// that exist only in the device link. On the HOST build (SDK-free unit tests,
// which also compile sequencer_task.cpp) there are no such symbols and no two
// real stacks, so the guard compiles to a harmless no-op there.

#if defined(PICO_ON_DEVICE) || defined(__arm__)
#define GENSEQ_STACK_GUARD_ACTIVE 1
#else
#define GENSEQ_STACK_GUARD_ACTIVE 0
#endif

// Temporary diagnostic: fill the whole inter-stack gap at init to measure the
// deepest core0 stack excursion. OFF by default; pass -D to enable.
#ifndef GENSEQ_STACK_GUARD_FILL_GAP
#define GENSEQ_STACK_GUARD_FILL_GAP 0
#endif

#if GENSEQ_STACK_GUARD_ACTIVE
extern "C" {
extern uint32_t __StackOneTop;  // top (high addr, exclusive) of core1 stack == gap low edge
extern uint32_t __StackBottom;  // bottom (low addr) of core0 stack == gap high edge
}
#endif

namespace sequencer {

// Published fault state, readable over SWD and by core0 (e.g. H:dump). 0 =
// healthy; nonzero latches that the inter-stack gap was corrupted by an overflow.
inline volatile uint32_t g_stackGuardTripped = 0;

// Sentinels: distinctive, implausible as real stack data or a valid pointer.
static constexpr uint32_t kCanaryA = 0xC0DECAFEu;
static constexpr uint32_t kCanaryB = 0x57ACC0DEu;  // "STACCODE"

// Canary placement within the gap [__StackOneTop .. __StackBottom):
//
//   SEVERE canary @ __StackOneTop -- the word ADJACENT to core1's stack. A core0
//     overflow only reaches it after running the ENTIRE gap width down; if it is
//     clobbered, core1's stack is about to be / just was corrupted. THIS is the
//     alarm that matters -- the BUG A / BUG B-pt2 condition.
//
//   MARGIN canary @ __StackBottom - kMarginBytes -- an EARLY warning placed a
//     safety margin BELOW core0's stack bottom, NOT at the exact edge. core0's
//     normal deep boot path legitimately reaches __StackBottom and may graze ~1
//     word past it into the gap (harmless -- the ~2 KB gap absorbs it), so a
//     canary at __StackBottom-4 false-trips (observed). MEASURED (gap-fill scan):
//     the healthy boot's deepest core0 write is ~360 B past __StackBottom, so a
//     64 B margin tripped every boot. 1 KB sits well beyond that and still leaves
//     ~1 KB before core1, so it fires only on a real runaway.
static constexpr uintptr_t kMarginBytes = 1024u;

#if GENSEQ_STACK_GUARD_ACTIVE
inline volatile uint32_t* severeCanary() { return reinterpret_cast<volatile uint32_t*>(&__StackOneTop); }
inline volatile uint32_t* marginCanary() { return reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(&__StackBottom) - kMarginBytes); }

// Call ONCE on core0 before launching core1 (both stacks idle; gap untouched).
inline void stackGuardInit() {
#if GENSEQ_STACK_GUARD_FILL_GAP
    // DIAGNOSTIC (temporary): fill the ENTIRE inter-stack gap with a known
    // pattern so a post-boot scan can find the LOWEST surviving word = the
    // deepest core0 stack excursion into the gap (true overflow depth). The gap
    // is [__StackOneTop .. __StackBottom); fill high->low. Off by default.
    {
        volatile uint32_t* p   = reinterpret_cast<volatile uint32_t*>(&__StackOneTop);
        volatile uint32_t* end = reinterpret_cast<volatile uint32_t*>(&__StackBottom);
        uint32_t v = 0xA5A50000u;
        for (; p < end; ++p) *p = v++;   // each word distinct: scan finds the boundary
    }
#endif
    *severeCanary() = kCanaryB;  // core1-edge alarm (real corruption)
    *marginCanary() = kCanaryA;  // early warning, kMarginBytes below core0's bottom
    g_stackGuardTripped = 0;
}

// Cheap check (2 loads + compares). Latches a nonzero code into
// g_stackGuardTripped. Bit0 = MARGIN tripped (core0 overflowed >kMarginBytes past
// its bottom -- investigate). Bit1 = SEVERE (overflow reached core1's edge -- the
// BUG A / BUG B-pt2 corruption). Safe to call every core1 loop.
inline uint32_t stackGuardCheck() {
    uint32_t t = 0;
    if (*marginCanary() != kCanaryA) t |= 1u;
    if (*severeCanary() != kCanaryB) t |= 2u;
    if (t) g_stackGuardTripped |= t;
    return g_stackGuardTripped;
}
#else   // HOST build: no real stacks / linker symbols -> no-ops.
inline void stackGuardInit() { g_stackGuardTripped = 0; }
inline uint32_t stackGuardCheck() { return 0; }
#endif

}  // namespace sequencer
