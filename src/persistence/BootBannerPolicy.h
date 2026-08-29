#pragma once

#include "SaveRecord.h"  // for persistence::LoadStatus

namespace persistence {

// Returns true iff a boot-time load failure should raise the user-facing
// "saved config not loaded" banner. Only CRC_MISMATCH qualifies: magic and
// format version both matched (so a real GenSeq save of this version existed)
// but its payload did not verify - a genuine corrupt-save the user should know
// about. Every other status (OK / ABSENT / BAD_MAGIC / BAD_VERSION / TRUNCATED)
// means "no usable save of ours is present" (erased flash, foreign/stale bytes,
// or a malformed span) and must fall back to defaults silently.
inline bool shouldWarnOnBootLoad(LoadStatus status) {
    return status == LoadStatus::CRC_MISMATCH;
}

}  // namespace persistence
