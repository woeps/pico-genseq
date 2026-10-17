#include "KeyboardDecoder.h"

namespace hardware {

KeyboardDecoder::KeyboardDecoder(EventSink sink)
    : sink(std::move(sink)),
      downKeys{},
      mods(ui::mod::NONE),
      repeatKey(ui::KeyId::NONE),
      nextRepeatMs(0),
      lastNowMs(0)
{}

// Right-hand modifiers live in the high nibble; fold them onto their
// left-hand twins so shift is shift wherever it came from.
uint8_t KeyboardDecoder::foldMods(uint8_t raw)
{
    return static_cast<uint8_t>((raw & 0x0F) | ((raw >> 4) & 0x0F));
}

bool KeyboardDecoder::isDown(uint8_t usage) const
{
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        if (downKeys[i] == usage) return true;
    }
    return false;
}

void KeyboardDecoder::onReport(const KeyReport& report, uint32_t nowMs)
{
    // More keys down than the boot protocol can express: every slot reads
    // ErrorRollOver. Discard the whole report rather than read six phantoms.
    bool allRollOver = true;
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        if (report.keys[i] != ERROR_ROLL_OVER) { allRollOver = false; break; }
    }
    if (allRollOver) return;

    lastNowMs = nowMs;
    mods = foldMods(report.mods);

    // Releases: down before, absent now.
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        const uint8_t usage = downKeys[i];
        if (usage == 0) continue;

        bool stillDown = false;
        for (uint8_t j = 0; j < MAX_KEYS; ++j) {
            if (report.keys[j] == usage) { stillDown = true; break; }
        }
        if (stillDown) continue;

        const ui::KeyId id = static_cast<ui::KeyId>(usage);
        downKeys[i] = 0;
        if (id == repeatKey) repeatKey = ui::KeyId::NONE;
        sink(ui::events::Event::keyReleased(id, mods, nowMs));
    }

    // Presses: present now, not down before. The last one wins the repeat.
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        const uint8_t usage = report.keys[i];
        if (usage == 0 || isDown(usage)) continue;

        for (uint8_t j = 0; j < MAX_KEYS; ++j) {
            if (downKeys[j] == 0) { downKeys[j] = usage; break; }
        }

        const ui::KeyId id = static_cast<ui::KeyId>(usage);
        repeatKey = id;
        nextRepeatMs = nowMs + REPEAT_DELAY_MS;
        sink(ui::events::Event::keyPressed(id, mods, nowMs));
    }
}

void KeyboardDecoder::tick(uint32_t nowMs)
{
    lastNowMs = nowMs;
    if (repeatKey == ui::KeyId::NONE) return;

    // Wrap-safe deadline test: survives the 32-bit millisecond rollover.
    if (static_cast<int32_t>(nowMs - nextRepeatMs) < 0) return;

    // Schedule forward from now rather than accumulating, so a late tick
    // delays the next repeat instead of discharging a burst.
    nextRepeatMs = nowMs + REPEAT_INTERVAL_MS;
    sink(ui::events::Event::keyHeld(repeatKey, mods, nowMs));
}

void KeyboardDecoder::onDisconnect()
{
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        if (downKeys[i] == 0) continue;
        sink(ui::events::Event::keyReleased(
            static_cast<ui::KeyId>(downKeys[i]), mods, lastNowMs));
        downKeys[i] = 0;
    }

    mods = ui::mod::NONE;
    repeatKey = ui::KeyId::NONE;
    nextRepeatMs = 0;
}

} // namespace hardware
