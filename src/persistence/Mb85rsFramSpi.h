#pragma once

#include <cstddef>
#include <cstdint>

#include "Mb85rsFram.h"

// Firmware-only real SPI transport for the MB85RS2MTA FRAM (task 9.3 / 10.3).
//
// Implements persistence::IFramSpi over the Pico SDK spi_* API with an explicit
// software chip-select, so the save path is ordinary SPI traffic that NEVER
// disables XIP — the property that makes a FRAM save transparent to the running
// sequencer (contrast FlashStore, which parks both cores for ~37 ms).
//
// This header pulls in Pico SDK headers, so it is FIRMWARE-ONLY and must not be
// added to the host build. The host build tests the device logic through the
// SDK-free Mb85rsFram + FakeFramSpi instead.
//
// Default wiring (TODO.md 9.3 plan): SPI1 on GP10-13, kept OFF the QSPI/XIP bus.
//   GP10 = SCK, GP11 = MOSI (TX), GP12 = MISO (RX), GP13 = CS (software-driven).
// VERIFY these do not clash with the LCD's runtime-assigned I2C pins before
// wiring (see TODO.md). SPI mode 0, 8 MHz to start (the part supports 40 MHz).

#include "hardware/spi.h"
#include "hardware/gpio.h"

namespace persistence {

class Mb85rsFramSpi : public IFramSpi {
   public:
    struct Pins {
        spi_inst_t* instance = spi1;
        uint sck = 10;
        uint mosi = 11;
        uint miso = 12;
        uint cs = 13;
        uint baud = 8u * 1000u * 1000u;  // 8 MHz; part supports up to 40 MHz
    };

    Mb85rsFramSpi() : pins_(Pins{}) {}
    explicit Mb85rsFramSpi(const Pins& pins) : pins_(pins) {}

    // One-time hardware bring-up. Call before using the store as a backend.
    void init() {
        spi_init(pins_.instance, pins_.baud);
        spi_set_format(pins_.instance, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
        gpio_set_function(pins_.sck, GPIO_FUNC_SPI);
        gpio_set_function(pins_.mosi, GPIO_FUNC_SPI);
        gpio_set_function(pins_.miso, GPIO_FUNC_SPI);
        // Software CS: active-low, idle high.
        gpio_init(pins_.cs);
        gpio_set_dir(pins_.cs, GPIO_OUT);
        gpio_put(pins_.cs, 1);
    }

    void select() override { gpio_put(pins_.cs, 0); }
    void deselect() override { gpio_put(pins_.cs, 1); }

    void transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) override {
        if (len == 0) return;
        if (tx && rx) {
            spi_write_read_blocking(pins_.instance, tx, rx, len);
        } else if (tx) {
            spi_write_blocking(pins_.instance, tx, len);
        } else {
            // Read-only: clock out zeros, capture rx.
            spi_read_blocking(pins_.instance, 0x00, rx, len);
        }
    }

   private:
    Pins pins_;
};

}  // namespace persistence
