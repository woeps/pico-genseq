#pragma once

#include <cstdint>

#include "IFlashStore.h"
#include "PersistableConfig.h"
#include "PersistenceManager.h"
#include "SaveSnapshot.h"

// Bounded, nonblocking save-service orchestration (performance-safe-foundation
// task 8.2, Requirements 8.5-8.9).
//
// The existing PersistenceManager::save() does the whole Save_Operation in one
// synchronous call. That is fine for the flash write itself (which parks core1
// cooperatively), but the UI loop must not be MONOPOLIZED by orchestration
// around it (Req 8.6: no UI/input service gap > 100 ms during save; Req 8.7:
// > 50 ms otherwise). This service turns a save into an explicit, step-driven
// state machine the core0 UI loop pumps once per iteration, so each step is
// bounded and the loop stays responsive:
//
//   Idle --begin()--> Snapshot --service()--> Writing --service()--> Done/Failed
//
//   - begin(working) captures an immutable SaveSnapshot (task 8.1) of the
//     ACCEPTED Working State — cheap, allocation-free (Req 8.1-8.4).
//   - the first service() serializes + capacity-checks + writes via the manager
//     (the write parks core1 cooperatively; it is the one necessarily-long step,
//     bounded by the flash op itself, not by orchestration).
//   - the terminal state exposes a DISTINCT outcome (Req 8.9) the UI maps to its
//     success/failure matrix feedback.
//
// A UI/input service-gap tracker (Req 8.6/8.7) records the longest gap observed
// between service() calls so the firmware integration and host tests can assert
// the loop kept pumping. SDK-free: depends only on the pure IFlashStore /
// manager / snapshot, so it is host-testable against the in-memory fake store.

namespace persistence {

enum class SaveState : uint8_t {
    Idle,         // no save in progress
    Snapshotted,  // Working State captured, write not yet attempted
    Done,         // completed successfully
    Failed,       // completed with a failure outcome
};

class SaveService {
public:
    explicit SaveService(PersistenceManager& manager) : manager_(manager) {}

    // Begin a save: capture an immutable snapshot of the accepted Working State.
    // Returns false (and does nothing) if a save is already in progress, so a
    // double Ctrl+S cannot start two overlapping saves. Cheap/bounded (Req 8.5).
    bool begin(const PersistableConfig& acceptedWorkingState) {
        if (state_ == SaveState::Snapshotted) return false;  // already in flight
        snapshot_.capture(acceptedWorkingState);
        state_ = SaveState::Snapshotted;
        outcome_ = SaveOutcome::SUCCESS;  // provisional; set on completion
        return true;
    }

    // Pump one bounded step. Call once per UI-loop iteration. `nowMicros` lets
    // the service track the gap since the previous call (Req 8.6/8.7). Returns
    // true while a save is still in progress (so the caller keeps pumping),
    // false when Idle/terminal.
    bool service(uint64_t nowMicros) {
        trackGap(nowMicros);
        switch (state_) {
            case SaveState::Snapshotted: {
                // The write step. The snapshot is immutable (Req 8.4), so even if
                // the UI edited Working State since begin(), THIS save writes the
                // captured bytes. The flash write parks core1 cooperatively.
                outcome_ = manager_.save(snapshot_.config());
                state_ = (outcome_ == SaveOutcome::SUCCESS) ? SaveState::Done
                                                            : SaveState::Failed;
                return false;  // terminal after this step
            }
            case SaveState::Idle:
            case SaveState::Done:
            case SaveState::Failed:
            default:
                return false;
        }
    }

    // Acknowledge a terminal result and return to Idle so a new save can begin.
    void acknowledge() {
        if (state_ == SaveState::Done || state_ == SaveState::Failed) {
            state_ = SaveState::Idle;
            snapshot_.clear();
        }
    }

    SaveState state() const { return state_; }
    bool inProgress() const { return state_ == SaveState::Snapshotted; }
    bool succeeded() const { return state_ == SaveState::Done; }
    bool failed() const { return state_ == SaveState::Failed; }
    SaveOutcome outcome() const { return outcome_; }

    // Longest observed gap (microseconds) between consecutive service() calls
    // while a save was in progress — the UI/input responsiveness metric
    // (Req 8.6/8.7). Reset with resetGapMetric().
    uint64_t maxServiceGapMicros() const { return maxGapMicros_; }
    void resetGapMetric() { maxGapMicros_ = 0; lastServiceMicros_ = 0; haveLast_ = false; }

private:
    void trackGap(uint64_t nowMicros) {
        if (haveLast_) {
            const uint64_t gap = nowMicros - lastServiceMicros_;
            if (gap > maxGapMicros_) maxGapMicros_ = gap;
        }
        lastServiceMicros_ = nowMicros;
        haveLast_ = true;
    }

    PersistenceManager& manager_;
    SaveSnapshot snapshot_;
    SaveState state_ = SaveState::Idle;
    SaveOutcome outcome_ = SaveOutcome::SUCCESS;
    uint64_t maxGapMicros_ = 0;
    uint64_t lastServiceMicros_ = 0;
    bool haveLast_ = false;
};

}  // namespace persistence
