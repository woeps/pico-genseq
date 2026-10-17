#pragma once

#include <cstddef>
#include <cstdint>

#include "IFlashStore.h"

namespace persistence {

// Storage_Region reserved at the TOP of flash so it never collides with the
// program image, which grows from offset 0 upward. Reserving the top of flash
// guarantees no collision (Req 4.1).
//
// Size = STORAGE_SECTORS * FLASH_SECTOR_SIZE (4096) = 8192 bytes.
constexpr std::size_t STORAGE_SECTORS = 2;  // 8 KiB, see capacity check
constexpr std::size_t STORAGE_SIZE = STORAGE_SECTORS * 4096u;

// Concrete IFlashStore over the Pico SDK. This is the only component that
// touches real flash. It is firmware-only and is never added to the host build;
// the SDK includes live in FlashStore.cpp so this header stays SDK-free.
class FlashStore : public IFlashStore {
   public:
    // Computes the region offset at the top of flash
    // (PICO_FLASH_SIZE_BYTES - STORAGE_SIZE). Implemented in FlashStore.cpp.
    FlashStore();

    std::size_t capacity() const override { return STORAGE_SIZE; }

    void read(std::size_t offset, uint8_t* dst,
              std::size_t len) const override;

    FlashResult eraseAndProgram(const uint8_t* data, std::size_t len) override;

   private:
    std::size_t regionOffset_;  // = PICO_FLASH_SIZE_BYTES - STORAGE_SIZE
};

}  // namespace persistence
