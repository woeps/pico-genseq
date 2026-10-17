#pragma once

#include "IFlashStore.h"
#include "PersistableConfig.h"
#include "SaveRecord.h"

namespace persistence {

// Outcome of a Save_Operation orchestrated by PersistenceManager.
//   SUCCESS       - serialize + capacity check + flash write all succeeded.
//   FAIL_CAPACITY - serialized record exceeds the Storage_Region capacity;
//                   flash is never touched (checked before any erase/program).
//   FAIL_SNAPSHOT - the provided snapshot could not be serialized.
//   FAIL_FLASH    - the underlying flash erase/program/verify failed.
enum class SaveOutcome { SUCCESS, FAIL_CAPACITY, FAIL_SNAPSHOT, FAIL_FLASH };

// Orchestrates a Save_Operation and a boot load over an injected IFlashStore.
// Depends only on the pure IFlashStore interface and the SaveRecord codec, so
// it is host-testable against an in-memory fake store (no Pico SDK dependency).
// Runs on core0.
class PersistenceManager {
   public:
    // The store reference must outlive this manager.
    explicit PersistenceManager(IFlashStore& store) : store_(store) {}

    // Serialize `snapshot`, guard against capacity, then erase+program the
    // Storage_Region. See SaveOutcome for the failure taxonomy (Req 4.7).
    SaveOutcome save(const PersistableConfig& snapshot);

    // Boot load: read the region, validate, and decode into `out`. Returns the
    // LoadStatus; on any non-OK, `out` is left at its caller-provided defaults
    // and the region is untouched (read-only).
    LoadStatus load(PersistableConfig& out);

   private:
    IFlashStore& store_;
};

}  // namespace persistence
