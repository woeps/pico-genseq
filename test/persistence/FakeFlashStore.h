#pragma once

// In-memory fake flash store for the host build. Models the Storage_Region as a
// simple byte buffer so the codec and PersistenceManager can be exercised
// without the Pico SDK. Supports single-shot fault injection so failure paths
// (ERASE_FAILED / PROGRAM_FAILED / VERIFY_MISMATCH) can be tested.

#include "persistence/IFlashStore.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

class FakeFlashStore : public persistence::IFlashStore {
   public:
    explicit FakeFlashStore(std::size_t cap) : bytes_(cap, 0xFF), cap_(cap) {}

    std::size_t capacity() const override { return cap_; }

    void read(std::size_t off, uint8_t* dst, std::size_t len) const override {
        std::memcpy(dst, bytes_.data() + off, len);
    }

    persistence::FlashResult eraseAndProgram(const uint8_t* data,
                                             std::size_t len) override {
        if (len > cap_) {
            return persistence::FlashResult::PROGRAM_FAILED;
        }
        if (failNext_) {  // single-shot fault injection
            failNext_ = false;
            return injected_;
        }
        writeCount_++;
        std::memset(bytes_.data(), 0xFF, bytes_.size());  // model erase
        std::memcpy(bytes_.data(), data, len);            // model program
        return persistence::FlashResult::OK;
    }

    // Test helpers.
    int writeCount() const { return writeCount_; }
    void injectFailure(persistence::FlashResult r) {
        failNext_ = true;
        injected_ = r;
    }
    const std::vector<uint8_t>& raw() const { return bytes_; }

   private:
    std::vector<uint8_t> bytes_;
    std::size_t cap_;
    int writeCount_ = 0;
    bool failNext_ = false;
    persistence::FlashResult injected_ = persistence::FlashResult::OK;
};
