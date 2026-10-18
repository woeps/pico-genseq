#pragma once

// Host model of the MB85RS2MTA FRAM as seen over SPI, for driver unit tests
// (performance-safe-foundation task 9.3). It implements persistence::IFramSpi
// and emulates the device's command state machine: WREN/WRDI latch, RDSR,
// RDID, and READ/WRITE with a 3-byte (24-bit) address and internal auto-
// increment. A write with WEL clear is IGNORED (as the real part does), which
// is what makes the WREN-sequencing test meaningful. Optional fault injection
// corrupts the next write so the driver's read-back verify path is testable.

#include "persistence/Mb85rsFram.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class FakeFramSpi : public persistence::IFramSpi {
   public:
    FakeFramSpi() : array_(persistence::mb85rs::kCapacityBytes, 0x00) {}

    void select() override {
        selected_ = true;
        phase_ = Phase::Opcode;
        addr_ = 0;
        addrBytesSeen_ = 0;
    }

    void deselect() override {
        selected_ = false;
        // The real part auto-clears the write-enable latch at the end of a
        // completed WRITE frame. Model that so a second write without a fresh
        // WREN would fail — making the driver's WREN sequencing load-bearing.
        if (pendingWrite_ && wroteThisFrame_ > 0) {
            wel_ = false;
            pendingWrite_ = false;
        }
    }

    void transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) override {
        for (std::size_t i = 0; i < len; ++i) {
            const uint8_t out = tx ? tx[i] : 0x00;
            uint8_t in = 0x00;
            in = step(out);
            if (rx) rx[i] = in;
        }
    }

    // --- Test helpers --------------------------------------------------------
    const std::vector<uint8_t>& array() const { return array_; }
    bool writeEnableLatch() const { return wel_; }
    int writeOps() const { return writeOps_; }
    // Corrupt the Nth byte of the next WRITE payload (0-based) by flipping bits,
    // to drive the driver's VERIFY_MISMATCH path. -1 = no corruption.
    void corruptNextWriteAt(int index) { corruptAt_ = index; }
    // Set the two id bytes RDID returns (manufacturer, product proxy).
    void setDeviceId(uint8_t b0, uint8_t b1) { id0_ = b0; id1_ = b1; }

   private:
    enum class Phase { Opcode, Address, DataRead, DataWrite, Status, Id };

    uint8_t step(uint8_t out) {
        switch (phase_) {
            case Phase::Opcode:
                return handleOpcode(out);
            case Phase::Address:
                addr_ = (addr_ << 8) | out;
                if (++addrBytesSeen_ == persistence::mb85rs::kAddressBytes) {
                    addr_ &= (persistence::mb85rs::kCapacityBytes - 1);  // 18-bit wrap
                    phase_ = pendingWrite_ ? Phase::DataWrite : Phase::DataRead;
                    writeByteIndex_ = 0;
                }
                return 0x00;
            case Phase::DataRead: {
                const uint8_t v = array_[addr_];
                addr_ = (addr_ + 1) & (persistence::mb85rs::kCapacityBytes - 1);
                return v;
            }
            case Phase::DataWrite: {
                if (wel_) {
                    uint8_t v = out;
                    if (corruptAt_ >= 0 &&
                        static_cast<int>(writeByteIndex_) == corruptAt_) {
                        v = static_cast<uint8_t>(~v);  // corrupt exactly this byte
                        corruptAt_ = -1;
                    }
                    array_[addr_] = v;
                    addr_ = (addr_ + 1) & (persistence::mb85rs::kCapacityBytes - 1);
                    ++wroteThisFrame_;
                }
                ++writeByteIndex_;
                return 0x00;
            }
            case Phase::Status:
                // Return the status register (only WEL modeled).
                return wel_ ? persistence::mb85rs::SR_WEL : 0x00;
            case Phase::Id:
                return (idByteIndex_++ == 0) ? id0_ : id1_;
        }
        return 0x00;
    }

    uint8_t handleOpcode(uint8_t op) {
        pendingWrite_ = false;
        switch (op) {
            case persistence::mb85rs::WREN:
                wel_ = true;
                break;
            case persistence::mb85rs::WRDI:
                wel_ = false;
                break;
            case persistence::mb85rs::RDSR:
                phase_ = Phase::Status;
                break;
            case persistence::mb85rs::RDID:
                phase_ = Phase::Id;
                idByteIndex_ = 0;
                break;
            case persistence::mb85rs::READ:
                phase_ = Phase::Address;
                addr_ = 0;
                addrBytesSeen_ = 0;
                break;
            case persistence::mb85rs::WRITE:
                phase_ = Phase::Address;
                addr_ = 0;
                addrBytesSeen_ = 0;
                pendingWrite_ = true;
                wroteThisFrame_ = 0;
                // A completed WRITE frame clears WEL on the real part; we clear it
                // at deselect by counting ops. Count the op now.
                ++writeOps_;
                break;
            default:
                break;
        }
        return 0x00;
    }

    std::vector<uint8_t> array_;
    bool selected_ = false;
    bool wel_ = false;
    Phase phase_ = Phase::Opcode;
    uint32_t addr_ = 0;
    uint8_t addrBytesSeen_ = 0;
    bool pendingWrite_ = false;
    std::size_t writeByteIndex_ = 0;
    int wroteThisFrame_ = 0;
    int writeOps_ = 0;
    int corruptAt_ = -1;
    uint8_t id0_ = 0x04;  // Fujitsu manufacturer id (proxy)
    uint8_t id1_ = 0x48;  // product proxy
    uint8_t idByteIndex_ = 0;
};
