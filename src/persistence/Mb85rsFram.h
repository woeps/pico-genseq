#pragma once

#include <cstddef>
#include <cstdint>

#include "IFlashStore.h"

// MB85RS2MTA (Adafruit 4718) external SPI FRAM backend — performance-safe-
// foundation task 9.3 driver prep, behind the existing IFlashStore interface so
// the backend-neutral RecordStore (10.1) and SaveService (8.2) reuse unchanged.
//
// WHY FRAM FIXES THE SAVE DEFECT (the 9.2 finding): internal flash writes
// require disabling XIP, which freezes BOTH cores for ~37 ms. FRAM is an
// EXTERNAL SPI device with NO erase cycle and byte-level in-place writes, so a
// save is just SPI byte transfers from code that stays resident in flash the
// whole time — XIP is never disabled, neither core stalls, and the write is
// interruptible into bounded chunks the SaveService state machine can dribble
// out between ticks. This driver captures the device PROTOCOL; the transparency
// measurement is the bench step 9.3 once the part is wired.
//
// Part facts (MB85RS2MTA datasheet / Adafruit 4718):
//   - 2 Mbit = 256 KBytes, address space 0x00000..0x3FFFF.
//   - **3-byte (24-bit) address** — the #1 driver gotcha. The smaller MB85RS64/
//     256 parts use a 2-byte address; a 2-byte address here silently wraps at
//     64 KB. kAddressBytes is 3 and the address is emitted MSB-first.
//   - SPI mode 0 or 3, up to 40 MHz. No erase, no page boundary, no program
//     delay: a WRITE is WREN then opcode+addr+data in one CS-low frame, with the
//     device auto-incrementing its internal address across the streamed bytes.
//
// DESIGN: the device logic is SDK-FREE and host-testable. The bus is abstracted
// as IFramSpi with EXPLICIT chip-select framing — select(), transfer()* while
// selected, deselect() — which is exactly how the Pico SDK spi_* path works and
// avoids any ambiguity about what shares one CS frame. Firmware supplies a real
// IFramSpi over spi_* (that TU is firmware-only); host tests supply a
// FakeFramSpi that models the array + status register. So the opcode framing,
// 3-byte addressing, WREN sequencing, and read-back verify are fully exercised
// before the chip is in hand.

namespace persistence {

namespace mb85rs {
constexpr uint8_t WREN  = 0x06;  // set write-enable latch
constexpr uint8_t WRDI  = 0x04;  // clear write-enable latch
constexpr uint8_t RDSR  = 0x05;  // read status register
constexpr uint8_t WRSR  = 0x01;  // write status register
constexpr uint8_t READ  = 0x03;  // read memory, 3-byte address
constexpr uint8_t WRITE = 0x02;  // write memory, 3-byte address
constexpr uint8_t RDID  = 0x9F;  // read device id

constexpr uint8_t SR_WEL = 0x02;  // status register: write-enable latch bit

constexpr std::size_t kCapacityBytes = 256u * 1024u;  // 2 Mbit
constexpr uint8_t kAddressBytes = 3;                  // 24-bit address
}  // namespace mb85rs

// Explicitly-framed full-duplex SPI. A logical device operation is:
//   select(); transfer(...); transfer(...); deselect();
// All transfers between a select()/deselect() pair occur in ONE CS-low frame.
// `tx` is sent (0x00 when null); received bytes land in `rx` (ignored if null).
class IFramSpi {
   public:
    virtual ~IFramSpi() = default;
    virtual void select() = 0;
    virtual void transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) = 0;
    virtual void deselect() = 0;
};

// FRAM-backed IFlashStore. Exposes the SAME region size the flash backend used
// (8 KiB default) so the RecordStore A/B slot math is backend-independent; the
// part has far more room. The region is placed at FRAM offset 0.
class Mb85rsFram : public IFlashStore {
   public:
    explicit Mb85rsFram(IFramSpi& spi, std::size_t regionSize = 8192)
        : spi_(spi),
          regionSize_(regionSize > mb85rs::kCapacityBytes ? mb85rs::kCapacityBytes
                                                          : regionSize) {}

    std::size_t capacity() const override { return regionSize_; }

    void read(std::size_t offset, uint8_t* dst, std::size_t len) const override {
        if (offset >= regionSize_ || dst == nullptr || len == 0) return;
        if (offset + len > regionSize_) len = regionSize_ - offset;

        uint8_t header[1 + mb85rs::kAddressBytes];
        header[0] = mb85rs::READ;
        putAddress(&header[1], static_cast<uint32_t>(offset));

        spi_.select();
        spi_.transfer(header, nullptr, sizeof(header));  // opcode + 24-bit addr
        spi_.transfer(nullptr, dst, len);                // clock out the data
        spi_.deselect();
    }

    FlashResult eraseAndProgram(const uint8_t* data, std::size_t len) override {
        if (data == nullptr || len > regionSize_) return FlashResult::PROGRAM_FAILED;

        // FRAM has no erase. "eraseAndProgram" becomes WREN -> WRITE at offset 0
        // -> read-back verify. No XIP disable, no core pause — the whole point.
        if (!writeEnable()) return FlashResult::PROGRAM_FAILED;

        uint8_t header[1 + mb85rs::kAddressBytes];
        header[0] = mb85rs::WRITE;
        putAddress(&header[1], 0u);

        spi_.select();
        spi_.transfer(header, nullptr, sizeof(header));  // opcode + 24-bit addr
        spi_.transfer(data, nullptr, len);               // stream the record
        spi_.deselect();

        // Read-back verify in bounded chunks (no allocation).
        uint8_t chunk[64];
        for (std::size_t off = 0; off < len; off += sizeof(chunk)) {
            const std::size_t n = (len - off) < sizeof(chunk) ? (len - off) : sizeof(chunk);
            read(off, chunk, n);
            for (std::size_t i = 0; i < n; ++i) {
                if (chunk[i] != data[off + i]) return FlashResult::VERIFY_MISMATCH;
            }
        }
        return FlashResult::OK;
    }

    // Allocation-free, OFFSET-addressed write + read-back verify. Writes `len`
    // bytes at region `offset` WITHOUT buffering or rewriting the rest of the
    // region (FRAM is byte-writable with no erase, so a single A/B slot can be
    // updated in place). This is the path the live save uses: it performs no
    // heap allocation on the calling core, so a save on core0 cannot corrupt a
    // concurrent allocation on core1. Returns OK only if the read-back matches.
    FlashResult programAt(std::size_t offset, const uint8_t* data,
                          std::size_t len) {
        if (data == nullptr || len == 0) return FlashResult::PROGRAM_FAILED;
        if (offset + len > regionSize_) return FlashResult::PROGRAM_FAILED;
        if (!writeEnable()) return FlashResult::PROGRAM_FAILED;

        uint8_t header[1 + mb85rs::kAddressBytes];
        header[0] = mb85rs::WRITE;
        putAddress(&header[1], static_cast<uint32_t>(offset));

        spi_.select();
        spi_.transfer(header, nullptr, sizeof(header));  // opcode + 24-bit addr
        spi_.transfer(data, nullptr, len);               // stream the bytes
        spi_.deselect();

        // Read-back verify in bounded chunks (no allocation).
        uint8_t chunk[64];
        for (std::size_t off = 0; off < len; off += sizeof(chunk)) {
            const std::size_t n =
                (len - off) < sizeof(chunk) ? (len - off) : sizeof(chunk);
            read(offset + off, chunk, n);
            for (std::size_t i = 0; i < n; ++i) {
                if (chunk[i] != data[off + i]) return FlashResult::VERIFY_MISMATCH;
            }
        }
        return FlashResult::OK;
    }

    // Read the device id (manufacturer + product). Not part of IFlashStore;
    // used by firmware bring-up to confirm the right part is wired before trust-
    // ing it as the backend. Returns the first `len` id bytes into `id`.
    void readDeviceId(uint8_t* id, std::size_t len) {
        if (id == nullptr || len == 0) return;
        const uint8_t cmd = mb85rs::RDID;
        spi_.select();
        spi_.transfer(&cmd, nullptr, 1);
        spi_.transfer(nullptr, id, len);
        spi_.deselect();
    }

   private:
    static void putAddress(uint8_t* out, uint32_t addr) {
        out[0] = static_cast<uint8_t>((addr >> 16) & 0xFF);  // MSB first
        out[1] = static_cast<uint8_t>((addr >> 8) & 0xFF);
        out[2] = static_cast<uint8_t>(addr & 0xFF);
    }

    // Set the write-enable latch and confirm via RDSR that WEL stuck.
    bool writeEnable() {
        const uint8_t wren = mb85rs::WREN;
        spi_.select();
        spi_.transfer(&wren, nullptr, 1);
        spi_.deselect();

        const uint8_t rdsr = mb85rs::RDSR;
        uint8_t status = 0;
        spi_.select();
        spi_.transfer(&rdsr, nullptr, 1);
        spi_.transfer(nullptr, &status, 1);
        spi_.deselect();
        return (status & mb85rs::SR_WEL) != 0;
    }

    IFramSpi& spi_;
    std::size_t regionSize_;
};

}  // namespace persistence
