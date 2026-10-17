#include "FlashStore.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"

#include "../sequencer/core_pause.h"

namespace persistence {

namespace {

// The flash-touching routine MUST run from RAM: while a flash erase/program is
// in progress the XIP interface is disabled, so any instruction fetched from
// flash would fault. `__no_inline_not_in_flash_func` copies this function into
// RAM and prevents it being inlined back into a flash-resident caller (Req 4.3).
void __no_inline_not_in_flash_func(doFlashWrite)(uint32_t offset,
                                                 const uint8_t* buf,
                                                 size_t eraseBytes,
                                                 size_t programBytes) {
    flash_range_erase(offset, eraseBytes);
    flash_range_program(offset, buf, programBytes);
}

}  // namespace

FlashStore::FlashStore() {
    // Reserve the region at the TOP of flash. STORAGE_SIZE is a whole number of
    // FLASH_SECTOR_SIZE (4096) sectors and PICO_FLASH_SIZE_BYTES is a
    // power-of-two multiple of the sector size, so this offset is sector-aligned
    // (a hard requirement for flash_range_erase). Growing from offset 0, the
    // program image can never reach this region (Req 4.1).
    regionOffset_ = PICO_FLASH_SIZE_BYTES - STORAGE_SIZE;
}

void FlashStore::read(std::size_t offset, uint8_t* dst, std::size_t len) const {
    // Direct memory-mapped (XIP) read of the reserved region. No erase/program
    // is in progress here, so reading straight from the mapped flash is safe and
    // is intentionally read-only (Req 6.5, 6.6).
    std::memcpy(
        dst,
        reinterpret_cast<const uint8_t*>(XIP_BASE + regionOffset_ + offset),
        len);
}

FlashResult FlashStore::eraseAndProgram(const uint8_t* data, std::size_t len) {
    // Bounds guard: never accept a write larger than the reserved region, so no
    // write can ever spill toward the program image (Req 4.1).
    if (len > STORAGE_SIZE) {
        return FlashResult::PROGRAM_FAILED;
    }

    // Build a padded buffer sized to a whole number of program pages
    // (FLASH_PAGE_SIZE = 256). flash_range_program requires a page-multiple
    // length. Pad the unused tail with 0xFF (the erased state), so the verify
    // step below is exact against the same padded bytes.
    const std::size_t pageSpan =
        ((len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;

    // The erase must cover every sector (FLASH_SECTOR_SIZE = 4096) that the
    // programmed region touches. Round the programmed span up to whole sectors.
    std::size_t sectorSpan =
        ((pageSpan + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) *
        FLASH_SECTOR_SIZE;

    // Clamp both spans to the reserved region so nothing reaches the program
    // image even if the rounding above tried to exceed it (Req 4.1). pageSpan is
    // always <= sectorSpan <= STORAGE_SIZE given the len <= STORAGE_SIZE guard.
    if (sectorSpan > STORAGE_SIZE) {
        sectorSpan = STORAGE_SIZE;
    }

    uint8_t buffer[STORAGE_SIZE];
    std::memset(buffer, 0xFF, pageSpan);
    std::memcpy(buffer, data, len);

    // Park core1 for the entire erase/program window so both cores never touch
    // flash at once. We use a cooperative pause (NOT multicore_lockout, which
    // would hijack the command FIFO): core1 polls the pause request in its loop,
    // acknowledges, and busy-waits in RAM with interrupts disabled until we
    // release it (Req 4.4). corePauseRequestAndWait() returns once core1 has
    // acknowledged (or after a bounded safety spin).
    sequencer::corePauseRequestAndWait();

    // Disable interrupts on the writing core: any ISR resident in flash would
    // fault while XIP is down.
    uint32_t irq = save_and_disable_interrupts();

    // Perform the erase + program from the RAM-resident routine.
    doFlashWrite(static_cast<uint32_t>(regionOffset_), buffer, sectorSpan,
                 pageSpan);

    restore_interrupts(irq);

    // Release core1 from its cooperative busy-wait so it resumes the normal
    // command/update loop (Req 4.5).
    sequencer::corePauseRelease();

    // Read back the just-programmed bytes via the memory-mapped view and compare
    // against the intended data. Return OK only on an exact match; any
    // discrepancy (failed erase, failed program, or bit error) is reported as a
    // verify mismatch (Req 4.6, 4.8).
    const uint8_t* readback =
        reinterpret_cast<const uint8_t*>(XIP_BASE + regionOffset_);
    if (std::memcmp(readback, data, len) != 0) {
        return FlashResult::VERIFY_MISMATCH;
    }

    return FlashResult::OK;
}

}  // namespace persistence
