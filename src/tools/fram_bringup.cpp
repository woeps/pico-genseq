// performance-safe-foundation task 9.3 — on-target FRAM bring-up / wiring check.
//
// This is a STANDALONE diagnostic firmware, separate from `genseq`. It does the
// first on-target step the TODO.md 9.3 plan calls for: power on, init SPI1,
// read the MB85RS2MTA device id (RDID), and — only if the id looks healthy —
// prove the 3-byte addressing works on real silicon with a write/read-back
// round-trip high in the 256 KB space (then restore the original bytes).
//
// It uses USB-CDC stdio (not the UART the main build uses) so the result prints
// straight to /dev/ttyACM0 with no risk of colliding with the MIDI UART on
// GP8/9. It touches NOTHING in the live save path; nothing on the device is
// modified except the scratch address, which is read first and written back.
//
// Expected healthy RDID for the MB85RS2M: manufacturer byte 0x04 (Fujitsu),
// followed by continuation/density/product bytes. All-0x00 or all-0xFF means a
// wiring fault — almost always MISO/MOSI swapped or CS not reaching GP13.
//
// Build:  cmake -B build-fram -DGENSEQ_BUILD_FRAM_BRINGUP=ON && cmake --build build-fram --target fram_bringup
// Flash:  picotool load -x build-fram/fram_bringup.uf2   (or copy the .uf2 in BOOTSEL)

#include <cstdint>
#include <cstdio>

#include "pico/stdlib.h"

#include "persistence/Mb85rsFram.h"
#include "persistence/Mb85rsFramSpi.h"

namespace {

void printId(const uint8_t* id, std::size_t len) {
    std::printf("RDID bytes:");
    for (std::size_t i = 0; i < len; ++i) std::printf(" 0x%02X", id[i]);
    std::printf("\n");
}

bool looksHealthy(const uint8_t* id, std::size_t len) {
    // Fujitsu manufacturer id is 0x04. Reject the two classic wiring-fault
    // patterns (bus stuck low or stuck/pulled high).
    bool allZero = true, allFF = true;
    for (std::size_t i = 0; i < len; ++i) {
        if (id[i] != 0x00) allZero = false;
        if (id[i] != 0xFF) allFF = false;
    }
    if (allZero || allFF) return false;
    return id[0] == 0x04;
}

// Round-trip at a 24-bit address beyond 64 KB to prove the 3-byte addressing on
// real hardware. Reads the current bytes first and restores them afterwards so
// the device is left untouched.
void addressingRoundTrip(persistence::Mb85rsFramSpi& spi) {
    using namespace persistence::mb85rs;
    const uint32_t addr = 0x012345;  // 74565, > 64 KB — exercises the high byte

    auto frame = [&](const uint8_t* tx, uint8_t* rx, std::size_t len) {
        spi.select();
        spi.transfer(tx, rx, len);
        spi.deselect();
    };
    auto readAt = [&](uint32_t a, uint8_t* dst, std::size_t len) {
        uint8_t hdr[4] = {READ, uint8_t((a >> 16) & 0xFF), uint8_t((a >> 8) & 0xFF), uint8_t(a & 0xFF)};
        spi.select();
        spi.transfer(hdr, nullptr, 4);
        spi.transfer(nullptr, dst, len);
        spi.deselect();
    };
    auto writeAt = [&](uint32_t a, const uint8_t* src, std::size_t len) {
        uint8_t wren = WREN;
        frame(&wren, nullptr, 1);
        uint8_t hdr[4] = {WRITE, uint8_t((a >> 16) & 0xFF), uint8_t((a >> 8) & 0xFF), uint8_t(a & 0xFF)};
        spi.select();
        spi.transfer(hdr, nullptr, 4);
        spi.transfer(src, nullptr, len);
        spi.deselect();
    };

    uint8_t saved[4] = {0, 0, 0, 0};
    readAt(addr, saved, 4);

    const uint8_t probe[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    writeAt(addr, probe, 4);

    uint8_t got[4] = {0, 0, 0, 0};
    readAt(addr, got, 4);

    const bool ok = got[0] == 0xDE && got[1] == 0xAD && got[2] == 0xBE && got[3] == 0xEF;
    std::printf("3-byte addressing @0x%06lX: wrote DE AD BE EF, read %02X %02X %02X %02X -> %s\n",
                (unsigned long)addr, got[0], got[1], got[2], got[3], ok ? "PASS" : "FAIL");

    // Negative control: the 16-bit-truncated address must NOT hold the sentinel.
    uint8_t aliased[1] = {0};
    readAt(addr & 0xFFFF, aliased, 1);
    std::printf("negative control @0x%04lX: read 0x%02X -> %s (should NOT be 0xDE)\n",
                (unsigned long)(addr & 0xFFFF), aliased[0], aliased[0] != 0xDE ? "PASS" : "FAIL");

    writeAt(addr, saved, 4);  // restore
    std::printf("restored original bytes at scratch address\n");
}

}  // namespace

int main() {
    stdio_init_all();
    // Give the USB-CDC host time to attach so the first lines aren't lost.
    for (int i = 0; i < 30; ++i) {
        if (stdio_usb_connected()) break;
        sleep_ms(100);
    }

    std::printf("\n=== MB85RS2MTA FRAM bring-up (task 9.3) ===\n");
    std::printf("SPI1: SCK=GP10 MOSI=GP11 MISO=GP12 CS=GP13 @ 8 MHz\n");

    persistence::Mb85rsFramSpi spi;
    spi.init();

    bool announcedHealthy = false;
    while (true) {
        uint8_t id[4] = {0, 0, 0, 0};
        {
            using namespace persistence::mb85rs;
            const uint8_t cmd = RDID;
            spi.select();
            spi.transfer(&cmd, nullptr, 1);
            spi.transfer(nullptr, id, sizeof(id));
            spi.deselect();
        }
        printId(id, sizeof(id));

        if (looksHealthy(id, sizeof(id))) {
            std::printf("verdict: HEALTHY (Fujitsu 0x04) — wiring OK\n");
            if (!announcedHealthy) {
                addressingRoundTrip(spi);
                announcedHealthy = true;
                std::printf("bring-up complete; continuing to poll RDID once/sec\n");
            }
        } else {
            std::printf("verdict: WIRING FAULT — check MISO/MOSI not swapped, CS->GP13, 3V3 + GND\n");
            announcedHealthy = false;
        }
        sleep_ms(1000);
    }
    return 0;
}
