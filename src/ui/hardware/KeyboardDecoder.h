#pragma once

#include <cstdint>
#include <functional>
#include "../Event.h"

namespace hardware {

// The HID boot-protocol keyboard report, minus its reserved byte.
struct KeyReport {
    uint8_t mods;       // raw HID modifier byte
    uint8_t keys[6];    // usage codes currently down, 0 = empty slot
};

// Turns a stream of HID reports into key events. Pure logic: it never reads a
// clock, so the caller supplies nowMs and tests can inject time.
class KeyboardDecoder {
public:
    static constexpr uint32_t REPEAT_DELAY_MS = 400;
    static constexpr uint32_t REPEAT_INTERVAL_MS = 30;
    static constexpr uint8_t MAX_KEYS = 6;
    static constexpr uint8_t ERROR_ROLL_OVER = 0x01;

    static_assert(sizeof(KeyReport::keys) == MAX_KEYS,
                  "KeyReport::keys and MAX_KEYS must stay in sync");

    using EventSink = std::function<void(const ui::events::Event&)>;

    explicit KeyboardDecoder(EventSink sink);

    // Diff against the previous report; emits KEY_PRESSED / KEY_RELEASED.
    void onReport(const KeyReport& report, uint32_t nowMs);

    // Emits at most one KEY_HELD when the repeat deadline has passed. Must be
    // called regularly: a held key produces no further HID traffic.
    void tick(uint32_t nowMs);

    // Releases everything still down and clears all state.
    void onDisconnect();

private:
    EventSink sink;
    uint8_t downKeys[MAX_KEYS];
    uint8_t mods;
    ui::KeyId repeatKey;        // KeyId::NONE when nothing is repeating
    uint32_t nextRepeatMs;
    uint32_t lastNowMs;

    bool isDown(uint8_t usage) const;
    static uint8_t foldMods(uint8_t raw);
};

} // namespace hardware
