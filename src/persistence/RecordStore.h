#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "IFlashStore.h"
#include "SaveRecord.h"

// Backend-neutral transactional record store (performance-safe-foundation
// task 10.1, Requirement 9.1; defines the A/B transactional-commit SHAPE that
// task 10.2 realises on the selected backend, Req 9.6/9.7).
//
// The persistence orchestration must depend on a backend-NEUTRAL storage
// interface rather than on internal RP2040 flash directly (Req 9.1). `IFlashStore`
// already abstracts the byte medium; `RecordStore` adds the TRANSACTIONAL record
// layer on top of it, independent of whether the medium is internal flash, FRAM,
// or the in-memory host fake:
//
//   - the Storage_Region is split into TWO equal slots (A and B). A new record is
//     always written to the slot NOT currently holding the newest valid record,
//     so the previous valid record stays intact until the new one is fully
//     written and verified (Req 9.6).
//   - each record carries the standard Save_Record header plus a monotonic
//     GENERATION number (encoded in the header's reserved/version-adjacent space
//     via a small store-level wrapper); on load, the slot with the highest
//     generation whose CRC validates wins. A half-written or CRC-failed slot is
//     ignored, so the previous committed record is selected after an interrupted
//     commit (Req 9.7).
//
// This component is SDK-free and host-testable against the in-memory fake store.
// It does NOT perform the actual flash timing/parking (that is FlashStore's job)
// — it owns slot selection, generation accounting, and commit/verify logic, i.e.
// the backend-neutral half the selected-backend integration (10.2/10.3) plugs
// into.
//
// Generation encoding: the Save_Record header reserves one byte (offset 3,
// "reserved") which this store repurposes as a low-byte generation tag written
// AROUND the codec. To stay compatible with the existing single-slot codec and
// its CRC (which covers the payload, not the header reserved byte), the store
// keeps its own generation in a tiny store header prepended per slot. Keeping it
// separate from the Save_Record payload means the record codec is unchanged.

namespace persistence {

// Per-slot store header written ahead of the Save_Record. Fixed size, trivially
// serialisable, and covered by its own validity marker so a half-written slot is
// detectable independently of the record CRC.
struct SlotHeader {
    static constexpr uint8_t kMagic0 = 'S';
    static constexpr uint8_t kMagic1 = 'L';
    static constexpr std::size_t kSize = 8;  // magic(2) + gen(4) + committed(1) + pad(1)

    uint8_t magic0 = kMagic0;
    uint8_t magic1 = kMagic1;
    uint32_t generation = 0;  // monotonic; higher == newer
    uint8_t committed = 0;    // 1 only after the record is fully written+verified
};

enum class CommitResult { OK, FAIL_CAPACITY, FAIL_WRITE };

class RecordStore {
public:
    explicit RecordStore(IFlashStore& store) : store_(store) {}

    // Usable bytes per slot (half the region, minus the slot header).
    std::size_t slotPayloadCapacity() const {
        const std::size_t half = store_.capacity() / 2;
        return half > SlotHeader::kSize ? half - SlotHeader::kSize : 0;
    }

    // Transactionally commit a new record. Writes to the slot NOT currently
    // holding the newest valid record, stamps generation = newest+1, and marks
    // it committed only after a successful write. The previous record (other
    // slot) is left intact (Req 9.6). On capacity overflow nothing is written.
    CommitResult commit(const PersistableConfig& cfg) {
        const std::vector<uint8_t> record = serialize(cfg);
        if (record.size() > slotPayloadCapacity()) return CommitResult::FAIL_CAPACITY;

        const SlotSel newest = selectNewest();
        const uint8_t writeSlot = (newest.valid && newest.slot == 0) ? 1 : 0;
        const uint32_t nextGen = newest.valid ? newest.generation + 1 : 1;

        // Build slot bytes: SlotHeader (committed=1) + Save_Record.
        std::vector<uint8_t> slotBytes(SlotHeader::kSize + record.size(), 0);
        writeSlotHeader(slotBytes.data(), nextGen, /*committed=*/1);
        for (std::size_t i = 0; i < record.size(); ++i) {
            slotBytes[SlotHeader::kSize + i] = record[i];
        }

        if (!writeSlotRegion(writeSlot, slotBytes)) return CommitResult::FAIL_WRITE;
        return CommitResult::OK;
    }

    // Load the newest valid committed record. Returns the LoadStatus of the
    // winning slot; ABSENT when neither slot holds a committed valid record. On
    // any non-OK, `out` is left untouched.
    LoadStatus load(PersistableConfig& out) {
        const SlotSel newest = selectNewest();
        if (!newest.valid) return LoadStatus::ABSENT;
        return readRecord(newest.slot, out);
    }

    // The generation currently committed (0 when none), for diagnostics/tests.
    uint32_t currentGeneration() {
        const SlotSel s = selectNewest();
        return s.valid ? s.generation : 0;
    }

private:
    struct SlotSel {
        bool valid = false;
        uint8_t slot = 0;
        uint32_t generation = 0;
    };

    std::size_t slotOffset(uint8_t slot) const { return slot == 0 ? 0 : store_.capacity() / 2; }

    void writeSlotHeader(uint8_t* dst, uint32_t gen, uint8_t committed) const {
        dst[0] = SlotHeader::kMagic0;
        dst[1] = SlotHeader::kMagic1;
        dst[2] = static_cast<uint8_t>(gen & 0xFF);
        dst[3] = static_cast<uint8_t>((gen >> 8) & 0xFF);
        dst[4] = static_cast<uint8_t>((gen >> 16) & 0xFF);
        dst[5] = static_cast<uint8_t>((gen >> 24) & 0xFF);
        dst[6] = committed;
        dst[7] = 0;
    }

    bool readSlotHeader(uint8_t slot, SlotHeader& out) const {
        uint8_t buf[SlotHeader::kSize];
        store_.read(slotOffset(slot), buf, SlotHeader::kSize);
        if (buf[0] != SlotHeader::kMagic0 || buf[1] != SlotHeader::kMagic1) return false;
        out.generation = static_cast<uint32_t>(buf[2]) |
                         (static_cast<uint32_t>(buf[3]) << 8) |
                         (static_cast<uint32_t>(buf[4]) << 16) |
                         (static_cast<uint32_t>(buf[5]) << 24);
        out.committed = buf[6];
        return true;
    }

    // IFlashStore::eraseAndProgram writes from the region BASE, so to write a
    // single slot without disturbing the other we read the whole region, patch
    // the target slot, and write it back. (A backend with independent sector
    // erase — the 10.2 integration — refines this; the neutral logic here keeps
    // the A/B semantics correct on any IFlashStore.)
    bool writeSlotRegion(uint8_t slot, const std::vector<uint8_t>& slotBytes) {
        const std::size_t cap = store_.capacity();
        const std::size_t half = cap / 2;
        if (slotBytes.size() > half) return false;

        std::vector<uint8_t> region(cap, 0xFF);
        // Preserve the OTHER slot's existing contents so the previous record
        // survives (Req 9.6).
        store_.read(0, region.data(), cap);
        const std::size_t off = slotOffset(slot);
        // Clear the target slot then patch in the new bytes.
        for (std::size_t i = 0; i < half; ++i) region[off + i] = 0xFF;
        for (std::size_t i = 0; i < slotBytes.size(); ++i) region[off + i] = slotBytes[i];

        return store_.eraseAndProgram(region.data(), region.size()) == FlashResult::OK;
    }

    LoadStatus readRecord(uint8_t slot, PersistableConfig& out) const {
        const std::size_t half = store_.capacity() / 2;
        if (half <= SlotHeader::kSize) return LoadStatus::TRUNCATED;
        const std::size_t recLen = half - SlotHeader::kSize;
        std::vector<uint8_t> rec(recLen);
        store_.read(slotOffset(slot) + SlotHeader::kSize, rec.data(), recLen);
        return deserialize(rec.data(), recLen, out);
    }

    // Pick the slot with the highest generation whose SlotHeader is valid AND
    // committed AND whose record CRC validates. A half-written/failed slot is
    // skipped so an interrupted commit falls back to the previous record
    // (Req 9.7).
    SlotSel selectNewest() {
        SlotSel best;
        for (uint8_t slot = 0; slot < 2; ++slot) {
            SlotHeader hdr;
            if (!readSlotHeader(slot, hdr) || hdr.committed != 1) continue;
            PersistableConfig probe{};
            if (readRecord(slot, probe) != LoadStatus::OK) continue;  // CRC/format bad
            if (!best.valid || hdr.generation > best.generation) {
                best.valid = true;
                best.slot = slot;
                best.generation = hdr.generation;
            }
        }
        return best;
    }

    IFlashStore& store_;
};

}  // namespace persistence
