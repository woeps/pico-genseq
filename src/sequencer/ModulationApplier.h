#pragma once

#include "ModulationRegistry.h"
#include "ModulationTarget.h"
#include "ParameterBus.h"

#include <cstdint>

// task 17 — the per-tick modulation pass (FR11-FR14, AC3).
//
// ModulationApplier::process() runs ONCE PER SERVICED TICK, in the Sequencer
// tick loop BEFORE step evaluation, on the single authoritative transport
// position. For every configured ModulationTarget it:
//
//   1. derives the target's step index from the TransportPosition following
//      the target's beat/bar-relative step duration and playing order
//      (forward / reverse / ping-pong / random — deterministic from the
//      target's seed and the step counter, task 15 helpers);
//   2. reads the pattern step value and maps
//         balance + scaling * (step - center)
//      onto the target parameter's UI range (FR14), clamped;
//   3. writes the result as ONE ABSOLUTE overlay on the ParameterBus
//      (setOverlay) — the bus the Sequencer's step evaluation and transport
//      then read EFFECTIVE values from.
//
// It NEVER writes a base value, an undo snapshot or a saved baseline
// (AC3): the working state and the timeline's stored BPM are strictly
// read-only here, so modulation is non-destructive by construction.
//
// Overlay lifecycle: each pass REBUILDS the whole overlay set from scratch
// (clear all, then re-arm every active target). The tick-path overlay state
// is therefore a pure function of (registry configuration, position) —
// idempotent, never stale: disabling a target, zeroing its scaling (FR14:
// scale 0 is "modulation off"), releasing its slot or repointing its
// address drops its overlay on the very next tick. Targets that are
// inactive, disabled, scale-0, misconfigured (pattern index out of range)
// or still in factory configuration (deliberately INVALID address) modulate
// NOTHING — with nothing configured, process() leaves the bus exactly as it
// found it, i.e. Milestone 2 behaviour.
//
// Scope (FR13): a per-pattern target writes only its own pattern slot's
// parameter; a global target writes the single global parameter (the BPM,
// which the Sequencer's tempo window syncs into the transport timeline).
// Several targets on the same address: the highest slot index wins (the
// pass visits slots in order, overwriting).
//
// Performance: the work is BOUNDED by the registry's fixed capacity
// (MAX_TARGETS slots, MAX_PATTERNS patterns, MAX_STEPS steps — all
// compile-time), the loop body does no allocation and no unbounded work,
// and everything it touches is statically sized. SDK-free, no RTTI, no
// exceptions: <cstdint> only, plain integer math.

namespace sequencer {

class ModulationApplier {
public:
    // Write this tick's modulation overlays for `position` onto `bus`.
    // Allocation-free; total over any registry/bus state.
    static void process(const TransportPosition& position, ModulationRegistry& registry,
                        ParameterBus& bus);
};

inline void ModulationApplier::process(const TransportPosition& position,
                                       ModulationRegistry& registry, ParameterBus& bus) {
    // Rebuild the overlay set from scratch every tick (see file comment):
    // the overlay state is exactly f(configuration, position), so a target
    // that stopped modulating drops its overlay on this very tick.
    bus.clearAllOverlays();

    // Bounded by the fixed target capacity — never by configuration.
    for (size_t i = 0; i < ModulationRegistry::MAX_TARGETS; ++i) {
        if (!registry.targetActive(i)) continue;  // unclaimed slot
        const ModulationTarget& t = registry.target(i);

        if (!t.enabled) continue;      // disabled: its (cleared) overlay stays off
        if (t.scaleFactor == 0) continue;  // FR14: scale 0 == modulation off
        const ParamInfo* info = bus.paramInfo(t.address);
        if (info == nullptr) continue;  // factory address (INVALID): modulates nothing
        if (!registry.patternIndexValid(t.patternIndex)) continue;  // misconfiguration

        // Step index from the single authoritative position (task 15),
        // then the pattern's step value for it.
        const ModulationPattern& pattern = registry.pattern(t.patternIndex);
        const uint32_t stepIndex = t.stepIndexForTransport(position, pattern.length());
        const uint8_t stepValue = pattern.stepAt(stepIndex);

        // FR14 mapping onto the parameter's range, clamped, from the STORED
        // base (read-only). The overlay is the absolute effective value.
        const int32_t base = bus.baseValue(t.address);
        const int32_t output =
            t.applyToBase(base, info->minValue, info->maxValue, stepValue);
        bus.setOverlay(t.address, output);  // absolute; rejects nothing valid here
    }
}

}  // namespace sequencer
