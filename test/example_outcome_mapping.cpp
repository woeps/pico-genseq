// Feature: persist-settings, Example: FlashResult -> SaveOutcome mapping (Req 4.7, 4.8)
//
// Validates: Requirements 4.7, 4.8.
//
// Requirement 4.7 / design "PersistenceManager::save": the manager maps the underlying
// IFlashStore result onto a SaveOutcome:
//
//   FlashStore result        -> SaveOutcome
//   -----------------------     -----------------------
//   (write OK)                  SUCCESS
//   ERASE_FAILED                FAIL_FLASH        (Req 4.8)
//   PROGRAM_FAILED              FAIL_FLASH        (Req 4.8)
//   VERIFY_MISMATCH             FAIL_FLASH        (Req 4.8)
//   (record > capacity)         FAIL_CAPACITY     (capacity guard, no flash touched)
//
// This is an EXAMPLE test (not property-based). It drives PersistenceManager::save against a
// FakeFlashStore and asserts each outcome:
//   1. A normal-capacity store with a small valid config and no injected fault -> SUCCESS.
//   2. Each injected FlashResult failure (ERASE_FAILED / PROGRAM_FAILED / VERIFY_MISMATCH)
//      -> FAIL_FLASH. Fault injection is single-shot, so a fresh store is used per case.
//   3. An oversized config against a tiny capacity store -> FAIL_CAPACITY, with the capacity
//      guard running before any flash access (writeCount stays 0).
//
// Host-side only: includes the SDK-free persistence codec/manager headers plus the in-memory
// FakeFlashStore and the C++ standard library. It links genseq_persistence and no Pico SDK
// hardware libraries. It carries its own main(), so test/CMakeLists.txt builds it as its OWN
// executable and excludes it from the property-test glob (two main()s would fail to link).

#include "persistence/IFlashStore.h"
#include "persistence/PersistableConfig.h"
#include "persistence/PersistenceManager.h"
#include "persistence/SaveRecord.h"

#include "FakeFlashStore.h"

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

// A config that serializes larger than a tiny (HEADER_SIZE-byte) capacity: one pattern with
// full pitch and velocity sets. Serialized size is HEADER_SIZE (12) + fixed prefix (4) +
// per-pattern max (6 gate + 2 + 16 pitches + 2 + 16 velocities + 2 = 44) = 60 bytes >> 12.
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

}  // namespace

int main() {
    using namespace persistence;

    bool ok = true;

    // Case 1: OK write -> SUCCESS.
    {
        FakeFlashStore store(8192);
        PersistenceManager mgr(store);
        const PersistableConfig cfg = makeSmallConfig();

        const SaveOutcome outcome = mgr.save(cfg);
        expect(outcome == SaveOutcome::SUCCESS,
               "OK flash write did not map to SaveOutcome::SUCCESS", ok);
        expect(store.writeCount() == 1,
               "a successful save did not perform exactly one flash write", ok);
    }

    // Case 2: each injected FlashResult failure -> FAIL_FLASH. injectFailure is single-shot,
    // so a fresh store is used per case (equivalently, re-inject before each save).
    {
        FakeFlashStore store(8192);
        PersistenceManager mgr(store);
        store.injectFailure(FlashResult::ERASE_FAILED);
        const SaveOutcome outcome = mgr.save(makeSmallConfig());
        expect(outcome == SaveOutcome::FAIL_FLASH,
               "ERASE_FAILED did not map to SaveOutcome::FAIL_FLASH", ok);
    }
    {
        FakeFlashStore store(8192);
        PersistenceManager mgr(store);
        store.injectFailure(FlashResult::PROGRAM_FAILED);
        const SaveOutcome outcome = mgr.save(makeSmallConfig());
        expect(outcome == SaveOutcome::FAIL_FLASH,
               "PROGRAM_FAILED did not map to SaveOutcome::FAIL_FLASH", ok);
    }
    {
        FakeFlashStore store(8192);
        PersistenceManager mgr(store);
        store.injectFailure(FlashResult::VERIFY_MISMATCH);
        const SaveOutcome outcome = mgr.save(makeSmallConfig());
        expect(outcome == SaveOutcome::FAIL_FLASH,
               "VERIFY_MISMATCH did not map to SaveOutcome::FAIL_FLASH", ok);
    }

    // Case 3: oversized record vs tiny capacity -> FAIL_CAPACITY, guarded before flash access.
    {
        FakeFlashStore store(HEADER_SIZE);  // 12 bytes: too small for any real record
        PersistenceManager mgr(store);
        const PersistableConfig cfg = makeOversizedConfig();

        // Sanity: the serialized record really does exceed the tiny capacity.
        const std::vector<uint8_t> record = serialize(cfg);
        expect(record.size() > store.capacity(),
               "oversized config did not serialize larger than the tiny capacity", ok);

        const SaveOutcome outcome = mgr.save(cfg);
        expect(outcome == SaveOutcome::FAIL_CAPACITY,
               "oversized record did not map to SaveOutcome::FAIL_CAPACITY", ok);
        expect(store.writeCount() == 0,
               "capacity guard did not run before flash access (a write occurred)", ok);
    }

    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: FlashResult -> SaveOutcome mapping did not match the documented "
                     "taxonomy (Req 4.7, 4.8)\n");
        return 1;
    }

    std::printf("PASS: PersistenceManager::save maps flash results to SaveOutcome as documented "
                "(SUCCESS / FAIL_FLASH / FAIL_CAPACITY) (Req 4.7, 4.8)\n");
    return 0;
}
