#pragma once

#include <cstddef>
#include <cstdint>

// Per-operation timing configuration for the generative engine
// (generative-engine-expansion task 11, FR10, AC7).
//
// Milestone 2 gave every transport/live-edit operation one approved default
// boundary plus a single global override (TimingPolicy.h). The generative
// engine extends this to a THREE-LEVEL policy so each operation can apply
// immediately or quantized, resolved in a FIXED order:
//
//   Level 1  per-invocation override — the operator's explicit request wins;
//   Level 2  per-operation mode     — the operation's configured mode,
//                                      unless it is Inherit;
//   Level 3  global default         — the fallback for Inherit operations.
//
// This header owns the CONFIGURATION data: the generative-operation enum, the
// mode enum, the approved factory defaults and the statically-sized
// per-operation mode table. The resolution and the boundary math live in
// TimingPolicy.h next to the Milestone-2 resolver they extend, so both levels
// compute quantized targets from the ONE authoritative TransportTimeline.
//
// Static sizing: the mode table is a fixed 7-entry array; no heap, no locks,
// no runtime initialization — usable in the tick path.
// SDK-free: <cstddef>/<cstdint> only, no Pico SDK types.

namespace sequencer {

// The generative-engine operations whose application timing the three-level
// policy resolves. Distinct from the Milestone-2 transport OperationKind
// (Play/Stop/Mute/...): these are the recoverable musical edits of the
// generative layer, dispatched through the CommandInbox.
enum class GenerativeOperation : uint8_t {
    Mutate = 0,            // operator-triggered mutation of working state
    Undo = 1,              // pop the global undo stack (recovery)
    Revert = 2,            // restore the baseline, clear the stack (recovery)
    Keep = 3,              // commit working state as the new baseline (recovery)
    GeneratorChange = 4,   // swap a pattern's generator (structural)
    ScaleChange = 5,       // change a pattern's scale (structural)
    ModulationEnable = 6,  // enable/disable a modulation pattern (overlay)
    COUNT,                 // sentinel: fixed size of the mode table below
};

constexpr size_t GENERATIVE_OPERATION_COUNT =
    static_cast<size_t>(GenerativeOperation::COUNT);  // 7

// The configured application mode of one operation. Inherit is not a mode of
// its own: it delegates the operation to the global default (level 3).
enum class OperationTimingMode : uint8_t {
    Immediate = 0,     // apply on the next serviced tick
    QuantizeBeat = 1,   // apply at the next beat boundary
    QuantizeBar = 2,    // apply at the next bar boundary
    Inherit = 3,        // follow the global default
};

// Approved per-operation factory defaults (task 11). Every operation ships an
// EXPLICIT mode so the unconfigured box behaves identically; Inherit is the
// operator's way to delegate an operation to the global default. Rationale,
// following the Milestone-2 default philosophy:
//   - Recovery gestures (Undo/Revert/Keep) stay Immediate — the operator gets
//     out of an unwanted state the moment they ask (Req 6.2 family);
//   - Musical content changes (Mutate, ModulationEnable) quantize to the next
//     BEAT so generative material never changes mid-beat unasked;
//   - Structural changes (GeneratorChange, ScaleChange) quantize to the next
//     BAR, matching the Milestone-2 SetReplacement/SceneTransition defaults.
inline constexpr OperationTimingMode factoryDefaultMode(GenerativeOperation op) {
    switch (op) {
        case GenerativeOperation::Mutate:
        case GenerativeOperation::ModulationEnable:
            return OperationTimingMode::QuantizeBeat;
        case GenerativeOperation::GeneratorChange:
        case GenerativeOperation::ScaleChange:
            return OperationTimingMode::QuantizeBar;
        case GenerativeOperation::Undo:
        case GenerativeOperation::Revert:
        case GenerativeOperation::Keep:
            return OperationTimingMode::Immediate;
        case GenerativeOperation::COUNT:
            break;
    }
    return OperationTimingMode::Inherit;  // unconfigured ops inherit the global default
}

// The shipped global default (level 3): Immediate, so a fresh Inherit
// configuration never delays an operator request; quantize deliberately.
inline constexpr OperationTimingMode factoryGlobalTimingDefault() {
    return OperationTimingMode::Immediate;
}

// The statically-sized timing configuration: one mode per generative operation
// (level 2) plus the global default (level 3). Fixed footprint of 8 bytes, no
// allocation, trivially persistable.
struct OperationTimingConfig {
    // Level 2: the configured mode of each GenerativeOperation, indexed by
    // enum value. The initializers mirror the approved factory defaults.
    OperationTimingMode modes[GENERATIVE_OPERATION_COUNT] = {
        OperationTimingMode::QuantizeBeat,   // Mutate
        OperationTimingMode::Immediate,      // Undo
        OperationTimingMode::Immediate,      // Revert
        OperationTimingMode::Immediate,      // Keep
        OperationTimingMode::QuantizeBar,    // GeneratorChange
        OperationTimingMode::QuantizeBar,    // ScaleChange
        OperationTimingMode::QuantizeBeat,   // ModulationEnable
    };
    // Level 3: the global default applied to operations configured Inherit.
    OperationTimingMode globalDefault = OperationTimingMode::Immediate;

    // The approved factory configuration (factoryDefaultMode is the single
    // source of truth; the loop re-derives every entry).
    static constexpr OperationTimingConfig factoryDefaults() {
        OperationTimingConfig cfg{};
        for (size_t i = 0; i < GENERATIVE_OPERATION_COUNT; ++i) {
            cfg.modes[i] = factoryDefaultMode(static_cast<GenerativeOperation>(i));
        }
        cfg.globalDefault = factoryGlobalTimingDefault();
        return cfg;
    }

    // The configured mode of `op`. An out-of-range operation reads as Inherit
    // so it degrades to the global default instead of inventing timing.
    OperationTimingMode modeFor(GenerativeOperation op) const {
        const uint8_t idx = static_cast<uint8_t>(op);
        if (idx >= GENERATIVE_OPERATION_COUNT) return OperationTimingMode::Inherit;
        return modes[idx];
    }

    // Configure `op`'s mode (level 2). Out-of-range operations are ignored.
    void setMode(GenerativeOperation op, OperationTimingMode mode) {
        const uint8_t idx = static_cast<uint8_t>(op);
        if (idx >= GENERATIVE_OPERATION_COUNT) return;
        modes[idx] = mode;
    }

    // Configure / read the level-3 global default.
    void setGlobalDefault(OperationTimingMode mode) { globalDefault = mode; }
    OperationTimingMode globalDefaultMode() const { return globalDefault; }
};

}  // namespace sequencer
