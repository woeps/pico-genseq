#pragma once

#include <cstdint>

#include "PersistableConfig.h"

// Save Snapshot semantics (performance-safe-foundation task 8.1,
// Requirements 8.1-8.4).
//
// When Save is requested, the persistence coordinator must capture ONE immutable,
// internally consistent copy of accepted Working State (Req 8.1) that:
//   - INCLUDES accepted working changes even when their audible application is
//     still pending a quantized boundary (Req 8.2) — the snapshot reflects
//     ACCEPTED intent, not what core1 has reached;
//   - EXCLUDES unkept audition drafts (Req 8.3) — only Working State is captured;
//   - is FROZEN at capture: later edits to the live Working State cannot mutate an
//     in-progress snapshot (Req 8.4).
//
// `SaveSnapshot` is that immutable capture. It holds a by-VALUE copy of the
// accepted `PersistableConfig` taken at `capture()` time; because it is a value
// copy, nothing the UI mutates afterward can reach into it. The source of the
// "accepted, drafts-excluded" distinction is the caller: it passes the committed
// Working State (not the audition draft) to capture(). This component guarantees
// the immutability half of the contract and is SDK-free / host-testable.
//
// Capture is a plain struct copy (no heap), so it is cheap and allocation-free on
// core0 and never touches the real-time path (Req 8.5 is the orchestration task
// 8.2's concern; here we only define the snapshot object).

namespace persistence {

class SaveSnapshot {
public:
    SaveSnapshot() = default;

    // Capture an immutable copy of the ACCEPTED Working State. The caller is
    // responsible for passing committed state (drafts already excluded, accepted
    // quantized changes already folded in). After this returns the snapshot is
    // frozen: a later capture() replaces it wholesale, but no reference into the
    // live state remains, so an edit to the caller's working state cannot change
    // THIS captured value.
    void capture(const PersistableConfig& acceptedWorkingState) {
        config_ = acceptedWorkingState;  // by-value copy — the freeze (Req 8.4)
        valid_ = true;
    }

    bool valid() const { return valid_; }

    // The frozen snapshot. Const access only — a snapshot is never edited in
    // place; a new Save captures a fresh one.
    const PersistableConfig& config() const { return config_; }

    void clear() { valid_ = false; }

private:
    PersistableConfig config_{};
    bool valid_ = false;
};

}  // namespace persistence
