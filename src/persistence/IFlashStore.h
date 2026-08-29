#pragma once

#include <cstddef>
#include <cstdint>

namespace persistence {

// Result of a flash write attempt.
enum class FlashResult { OK, ERASE_FAILED, PROGRAM_FAILED, VERIFY_MISMATCH };

// Abstract, byte-addressed view of the Storage_Region only. Offsets are always
// relative to the region base, never absolute flash addresses. Isolating flash
// access behind this pure interface keeps the codec and orchestration
// host-testable against an in-memory fake (no Pico SDK dependency here).
class IFlashStore {
   public:
    virtual ~IFlashStore() = default;

    // Total usable capacity of the Storage_Region in bytes.
    virtual std::size_t capacity() const = 0;

    // Copy `len` bytes starting at region offset `offset` into `dst`.
    // On hardware this is a memcpy from XIP_BASE + regionOffset + offset.
    virtual void read(std::size_t offset, uint8_t* dst,
                      std::size_t len) const = 0;

    // Erase the sector(s) covering [0, len) and program `data` from the region
    // base. On hardware this parks the other core and disables IRQs internally.
    // Returns OK only if erase, program, and read-back verify all succeed.
    virtual FlashResult eraseAndProgram(const uint8_t* data,
                                        std::size_t len) = 0;
};

}  // namespace persistence
