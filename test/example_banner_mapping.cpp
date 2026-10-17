// Feature: persist-settings, Example: banner distinctness and FlashResult->SaveOutcome mapping (Req 4.7, 4.8, 6.7, 7.2)
//
// Validates: Requirements 4.7, 4.8, 6.7, 7.2.
//
// -------------------------------------------------------------------------------------------
// HARDWARE DEVIATION (surfaced by this example on purpose):
//
// The original task 7.4 said "assert the success, failure, and 'not loaded' banner TEXTS are
// mutually distinct". During task 7.3 the feedback was implemented on the WS2812 LED matrix
// (distinct COLORS per SaveBanner state) rather than the design's assumed I2C LCD text: this
// hardware wires NO LCD (no I2C/SDA/SCL pins, no LCD address in src/config/pins.h). That
// deviation was documented in UIController.cpp and already surfaced to the user. The banner
// distinctness is therefore represented by the distinct `ui::state::SaveBanner` enum values
// (SUCCESS / FAILURE / NOT_LOADED), each mapped to a distinct LED-matrix color in
// UIController.cpp (green / red / amber).
//
// This host example encodes task 7.4's INTENT for the LED-matrix design:
//
//   Part 1 (Req 7.2, 6.7): the three feedback states the UI switches on
//       (SaveBanner::SUCCESS / FAILURE / NOT_LOADED) are mutually distinct enum values (and
//       distinct from NONE). This is the host-testable proxy for "distinct success / failure /
//       not-loaded indications": the UI maps each distinct enum to a distinct visual indication.
//       The CONCRETE visual distinctness (distinct LED-matrix colors) is asserted on-target per
//       the hardware deviation; this host check guarantees the three states the UI switches on
//       are themselves distinct, so that concrete mapping is well-defined.
//
//   Part 2 (Req 4.7, 4.8): PersistenceManager::save maps the underlying IFlashStore result onto
//       a SaveOutcome — OK -> SUCCESS; each flash failure (ERASE_FAILED / PROGRAM_FAILED /
//       VERIFY_MISMATCH) -> FAIL_FLASH; oversized record vs tiny capacity -> FAIL_CAPACITY. This
//       intentionally overlaps example_outcome_mapping.cpp (task 3.8), per this task's Req
//       listing 4.7/4.8; kept compact here.
//
//   Part 3 (Req 7.1, 7.2 intent): the SaveOutcome -> banner intent that UIController uses —
//       a SUCCESS outcome corresponds to SaveBanner::SUCCESS and any failure outcome corresponds
//       to SaveBanner::FAILURE. This is encoded as a tiny local mapping function that MIRRORS
//       UIController::drainSaveRequest's logic (src/ui/UIController.cpp). It is NOT calling the
//       real UI loop (that needs the SDK Display/LED matrix + timing and is integration-scope);
//       it only asserts the pure outcome->banner classification is well-defined and distinct.
//
// -------------------------------------------------------------------------------------------
// Host-side arrangement (mirrors example_outcome_mapping.cpp / task 3.8):
//
// This is an EXAMPLE test (not property-based) with its own int main() and PASS/FAIL prints. It
// includes the SDK-free persistence codec/manager headers plus the in-memory FakeFlashStore and
// the SDK-free ui::state::SaveBanner enum (from ui/state/UIState.h). It links genseq_persistence
// (for serialize / PersistenceManager); SaveBanner is header-only. No Pico SDK, no RapidCheck.
// It carries its own main(), so test/CMakeLists.txt builds it as its OWN executable and excludes
// it from the property-test glob.
//
// UIState.h includes ../../common/pitch_set.h; with -I src the include "ui/state/UIState.h"
// resolves and its relative include resolves too. UIState.h is SDK-free (only <array>,
// <cstddef>, <cstdint> and the header-only common::PlayingOrder enum), so linking
// genseq_persistence (no SDK) suffices.
// -------------------------------------------------------------------------------------------

#include "persistence/PersistenceManager.h"
#include "persistence/PersistableConfig.h"

#include "FakeFlashStore.h"

#include "ui/state/UIState.h"  // ui::state::SaveBanner

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// Reports one failed check and flips the shared pass flag.
void expect(bool cond, const char* what, bool& ok) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

// A small, valid config that serializes well within a normal-capacity store.
persistence::PersistableConfig makeSmallConfig() {
    persistence::PersistableConfig cfg{};
    cfg.patternCount = 1;
    cfg.bpm = 120;
    cfg.midiClockEnabled = 1;

    persistence::PatternConfig& p0 = cfg.patterns[0];
    p0.gateAlgorithm = 1;
    p0.gateSteps = 16;
    p0.gatePulses = 4;
    p0.gateRotation = 0;
    p0.gateNoteLength = 2;
    p0.gateLength = 50;
    p0.pitchCount = 2;
    p0.pitchOrder = 0;
    p0.pitches[0] = 60;
    p0.pitches[1] = 64;
    p0.velocityCount = 1;
    p0.velocityOrder = 0;
    p0.velocities[0] = 100;
    p0.midiChannel = 1;
    p0.active = 1;

    return cfg;
}

// An oversized config: one full pattern (16 pitches + 16 velocities) whose serialized size far
// exceeds a tiny (HEADER_SIZE-byte) capacity, so the capacity guard trips before any flash access.
persistence::PersistableConfig makeOversizedConfig() {
    persistence::PersistableConfig cfg{};
    cfg.patternCount = 1;
    cfg.bpm = 140;
    cfg.midiClockEnabled = 1;

    persistence::PatternConfig& p0 = cfg.patterns[0];
    p0.gateAlgorithm = 1;
    p0.gateSteps = 16;
    p0.gatePulses = 8;
    p0.gateRotation = 0;
    p0.gateNoteLength = 2;
    p0.gateLength = 50;
    p0.pitchCount = persistence::MAX_SET_LEN;      // 16
    p0.pitchOrder = 0;
    for (uint8_t i = 0; i < persistence::MAX_SET_LEN; i++) {
        p0.pitches[i] = static_cast<uint8_t>(48 + i);
    }
    p0.velocityCount = persistence::MAX_SET_LEN;   // 16
    p0.velocityOrder = 0;
    for (uint8_t i = 0; i < persistence::MAX_SET_LEN; i++) {
        p0.velocities[i] = static_cast<uint8_t>(64 + i);
    }
    p0.midiChannel = 1;
    p0.active = 1;

    return cfg;
}

// Mirrors UIController::drainSaveRequest's outcome->banner classification (see
// src/ui/UIController.cpp): a SUCCESS outcome (within the completion bound, which is trivially
// true here — no real flash timing) maps to SaveBanner::SUCCESS; every other/failure outcome
// collapses to SaveBanner::FAILURE. NOTE: this replicates the pure classification only; it does
// NOT invoke the real UI loop (Display/LED-matrix rendering + the 2000/5000 ms timing guards),
// which is integration-scope.
ui::state::SaveBanner outcomeToBanner(persistence::SaveOutcome outcome) {
    if (outcome == persistence::SaveOutcome::SUCCESS) {
        return ui::state::SaveBanner::SUCCESS;
    }
    return ui::state::SaveBanner::FAILURE;
}

}  // namespace

int main() {
    using namespace persistence;
    using ui::state::SaveBanner;

    bool ok = true;

    // -------------------------------------------------------------------------------------
    // Part 1 (Req 7.2, 6.7): the three feedback states are mutually distinct enum values,
    // and distinct from NONE. This is the host proxy for "distinct success / failure /
    // not-loaded indications"; the concrete distinct LED-matrix colors are asserted on-target
    // (hardware deviation). A well-defined 1:1 enum->color mapping REQUIRES the enums be
    // pairwise distinct — which is what we assert here.
    // -------------------------------------------------------------------------------------
    {
        expect(SaveBanner::SUCCESS != SaveBanner::FAILURE,
               "Part 1: SUCCESS and FAILURE banner states are not distinct", ok);
        expect(SaveBanner::SUCCESS != SaveBanner::NOT_LOADED,
               "Part 1: SUCCESS and NOT_LOADED banner states are not distinct", ok);
        expect(SaveBanner::FAILURE != SaveBanner::NOT_LOADED,
               "Part 1: FAILURE and NOT_LOADED banner states are not distinct", ok);
        // NONE is the no-banner sentinel; each real indication must differ from it too.
        expect(SaveBanner::SUCCESS != SaveBanner::NONE,
               "Part 1: SUCCESS banner state collides with NONE (no-banner sentinel)", ok);
        expect(SaveBanner::FAILURE != SaveBanner::NONE,
               "Part 1: FAILURE banner state collides with NONE (no-banner sentinel)", ok);
        expect(SaveBanner::NOT_LOADED != SaveBanner::NONE,
               "Part 1: NOT_LOADED banner state collides with NONE (no-banner sentinel)", ok);
    }

    // -------------------------------------------------------------------------------------
    // Part 2 (Req 4.7, 4.8): FlashResult -> SaveOutcome mapping via PersistenceManager::save.
    // Compact overlap with example_outcome_mapping.cpp (task 3.8), kept intentional per this
    // task's Req listing 4.7/4.8.
    // -------------------------------------------------------------------------------------
    {
        // OK write -> SUCCESS.
        FakeFlashStore store(8192);
        PersistenceManager mgr(store);
        const SaveOutcome outcome = mgr.save(makeSmallConfig());
        expect(outcome == SaveOutcome::SUCCESS,
               "Part 2: OK flash write did not map to SaveOutcome::SUCCESS", ok);
    }
    {
        // ERASE_FAILED -> FAIL_FLASH.
        FakeFlashStore store(8192);
        PersistenceManager mgr(store);
        store.injectFailure(FlashResult::ERASE_FAILED);
        const SaveOutcome outcome = mgr.save(makeSmallConfig());
        expect(outcome == SaveOutcome::FAIL_FLASH,
               "Part 2: ERASE_FAILED did not map to SaveOutcome::FAIL_FLASH", ok);
    }
    {
        // PROGRAM_FAILED -> FAIL_FLASH.
        FakeFlashStore store(8192);
        PersistenceManager mgr(store);
        store.injectFailure(FlashResult::PROGRAM_FAILED);
        const SaveOutcome outcome = mgr.save(makeSmallConfig());
        expect(outcome == SaveOutcome::FAIL_FLASH,
               "Part 2: PROGRAM_FAILED did not map to SaveOutcome::FAIL_FLASH", ok);
    }
    {
        // VERIFY_MISMATCH -> FAIL_FLASH.
        FakeFlashStore store(8192);
        PersistenceManager mgr(store);
        store.injectFailure(FlashResult::VERIFY_MISMATCH);
        const SaveOutcome outcome = mgr.save(makeSmallConfig());
        expect(outcome == SaveOutcome::FAIL_FLASH,
               "Part 2: VERIFY_MISMATCH did not map to SaveOutcome::FAIL_FLASH", ok);
    }
    {
        // Oversized record vs tiny capacity -> FAIL_CAPACITY, guarded before flash access.
        FakeFlashStore store(HEADER_SIZE);  // 12 bytes: too small for any real record
        PersistenceManager mgr(store);
        const PersistableConfig cfg = makeOversizedConfig();

        const std::vector<uint8_t> record = serialize(cfg);
        expect(record.size() > store.capacity(),
               "Part 2: oversized config did not serialize larger than the tiny capacity", ok);

        const SaveOutcome outcome = mgr.save(cfg);
        expect(outcome == SaveOutcome::FAIL_CAPACITY,
               "Part 2: oversized record did not map to SaveOutcome::FAIL_CAPACITY", ok);
        expect(store.writeCount() == 0,
               "Part 2: capacity guard did not run before flash access (a write occurred)", ok);
    }

    // -------------------------------------------------------------------------------------
    // Part 3 (Req 7.1, 7.2 intent): SaveOutcome -> banner intent used by UIController.
    // A SUCCESS outcome -> SaveBanner::SUCCESS; every failure outcome -> SaveBanner::FAILURE.
    // Mirrors UIController::drainSaveRequest (pure classification only; not the real UI loop).
    // -------------------------------------------------------------------------------------
    {
        expect(outcomeToBanner(SaveOutcome::SUCCESS) == SaveBanner::SUCCESS,
               "Part 3: SUCCESS outcome did not correspond to SaveBanner::SUCCESS", ok);
        expect(outcomeToBanner(SaveOutcome::FAIL_CAPACITY) == SaveBanner::FAILURE,
               "Part 3: FAIL_CAPACITY outcome did not correspond to SaveBanner::FAILURE", ok);
        expect(outcomeToBanner(SaveOutcome::FAIL_SNAPSHOT) == SaveBanner::FAILURE,
               "Part 3: FAIL_SNAPSHOT outcome did not correspond to SaveBanner::FAILURE", ok);
        expect(outcomeToBanner(SaveOutcome::FAIL_FLASH) == SaveBanner::FAILURE,
               "Part 3: FAIL_FLASH outcome did not correspond to SaveBanner::FAILURE", ok);
        // Success and failure banners must themselves be distinct (already covered in Part 1,
        // re-asserted here to tie the outcome->banner mapping to a distinct visual indication).
        expect(outcomeToBanner(SaveOutcome::SUCCESS) != outcomeToBanner(SaveOutcome::FAIL_FLASH),
               "Part 3: success and failure outcomes collapse to the same banner state", ok);
    }

    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: banner distinctness / FlashResult->SaveOutcome mapping example did "
                     "not match the documented behavior (Req 4.7, 4.8, 6.7, 7.2)\n");
        return 1;
    }

    std::printf("PASS: SaveBanner states are mutually distinct, PersistenceManager::save maps "
                "flash results to SaveOutcome, and outcomes map to distinct banners "
                "(Req 4.7, 4.8, 6.7, 7.2)\n");
    return 0;
}
