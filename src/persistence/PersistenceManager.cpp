#include "PersistenceManager.h"

#include <vector>

namespace persistence {

namespace {

// Read a little-endian uint32 from four raw bytes. Matches the Save_Record
// header encoding produced by serialize() (see SaveRecord.cpp appendU32LE).
uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

// Serialize the snapshot, guard against the Storage_Region capacity BEFORE any
// flash access (Req 3.5, 4.2), then erase+program and map the FlashResult onto
// a SaveOutcome (Req 4.7, 4.8).
SaveOutcome PersistenceManager::save(const PersistableConfig& snapshot) {
    const std::vector<uint8_t> bytes = serialize(snapshot);

    // Capacity check happens before any erase/program: an oversized record must
    // never touch flash (Req 3.5, 4.2).
    if (bytes.size() > store_.capacity()) {
        return SaveOutcome::FAIL_CAPACITY;
    }

    const FlashResult result = store_.eraseAndProgram(bytes.data(), bytes.size());
    switch (result) {
        case FlashResult::OK:
            return SaveOutcome::SUCCESS;
        case FlashResult::ERASE_FAILED:
        case FlashResult::PROGRAM_FAILED:
        case FlashResult::VERIFY_MISMATCH:
            return SaveOutcome::FAIL_FLASH;
    }

    // Unreachable: all FlashResult values are handled above. Return FAIL_FLASH
    // defensively for any future/unknown result.
    return SaveOutcome::FAIL_FLASH;
}

// Boot load: read the Storage_Region, validate, and decode into `out`. This is
// strictly read-only — no erase or program is ever issued (Req 6.5). On any
// non-OK LoadStatus the SaveRecord codec leaves `out` untouched, so the caller
// retains its firmware defaults (Req 5.2, 6.5).
LoadStatus PersistenceManager::load(PersistableConfig& out) {
    const std::size_t capacity = store_.capacity();

    // Guard: a region too small to even hold the fixed header cannot contain a
    // valid record. Treat it as truncated without mutating `out`.
    if (capacity < HEADER_SIZE) {
        return LoadStatus::TRUNCATED;
    }

    // Read the fixed 12-byte header first to learn the declared payload length.
    uint8_t header[HEADER_SIZE];
    store_.read(0, header, HEADER_SIZE);

    // Declared payload length lives at offset 4 (u32 LE), consistent with the
    // Save_Record layout emitted by serialize().
    const uint32_t payloadLength = readU32LE(header + 4);

    // Compute the total record size and clamp it to the region capacity so a
    // corrupt/oversized declared length can never read past the store. If the
    // declared payload does not fit, deserialize() will detect the shortfall
    // and return TRUNCATED.
    std::size_t total = HEADER_SIZE + static_cast<std::size_t>(payloadLength);
    if (total > capacity) {
        total = capacity;
    }

    // Read exactly `total` bytes (header + as much payload as fits) in one go.
    std::vector<uint8_t> buf(total);
    store_.read(0, buf.data(), total);

    // Delegate all validation (length, magic, version, crc, in that order) and
    // decoding to the codec. It guarantees `out` is never mutated on failure.
    return deserialize(buf.data(), total, out);
}

}  // namespace persistence
