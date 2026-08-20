#include "framework.h"
#include "ui/hardware/KeyboardDecoder.h"

#include <vector>

using namespace ui;
using hardware::KeyboardDecoder;
using hardware::KeyReport;

namespace {

struct Recorder {
    std::vector<events::Event> events;

    KeyboardDecoder::EventSink sink() {
        return [this](const events::Event& e) { events.push_back(e); };
    }

    void clear() { events.clear(); }
    size_t size() const { return events.size(); }
};

// Builds a boot-protocol report from up to six usage codes.
KeyReport report(uint8_t mods, std::initializer_list<uint8_t> keys) {
    KeyReport r{};
    r.mods = mods;
    uint8_t i = 0;
    for (uint8_t k : keys) { if (i < 6) r.keys[i++] = k; }
    return r;
}

constexpr uint8_t UP_USAGE   = static_cast<uint8_t>(KeyId::UP);
constexpr uint8_t DOWN_USAGE = static_cast<uint8_t>(KeyId::DOWN);
constexpr uint8_t A_USAGE    = static_cast<uint8_t>(KeyId::A);

} // namespace

TEST(decoder_emits_press_for_a_newly_down_key) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);

    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_PRESSED);
    CHECK(rec.events[0].data.key.id == KeyId::UP);
    CHECK_EQ(rec.events[0].data.key.mods, mod::NONE);
    CHECK_EQ(rec.events[0].timestamp, 1000);
}

TEST(decoder_emits_release_when_a_key_disappears) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {}), 1050);

    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_RELEASED);
    CHECK(rec.events[0].data.key.id == KeyId::UP);
}

TEST(decoder_does_not_re_emit_a_key_that_stays_down) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {UP_USAGE}), 1010);

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_ignores_slot_reshuffles) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE, A_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {A_USAGE, UP_USAGE}), 1010);   // same set, different slots

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_carries_modifiers_on_the_event) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(mod::SHIFT, {UP_USAGE}), 1000);

    CHECK_EQ(rec.size(), 1);
    CHECK_EQ(rec.events[0].data.key.mods, mod::SHIFT);
}

TEST(decoder_folds_right_hand_modifiers_onto_left) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0x20, {UP_USAGE}), 1000);   // right shift = bit 5

    CHECK_EQ(rec.size(), 1);
    CHECK_EQ(rec.events[0].data.key.mods, mod::SHIFT);
}

TEST(decoder_emits_nothing_for_a_modifier_only_change) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(mod::SHIFT, {}), 1000);

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_discards_roll_over_reports) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01}), 1010);

    CHECK_EQ(rec.size(), 0);   // no phantom presses, no spurious release of UP
}

TEST(decoder_tracks_a_second_key_pressed_and_released_independently) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {UP_USAGE, A_USAGE}), 1010);
    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_PRESSED);
    CHECK(rec.events[0].data.key.id == KeyId::A);

    rec.clear();
    d.onReport(report(0, {UP_USAGE}), 1020);
    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_RELEASED);
    CHECK(rec.events[0].data.key.id == KeyId::A);
}

TEST(decoder_repeats_after_the_delay_then_at_the_interval) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS - 1);
    CHECK_EQ(rec.size(), 0);

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS);
    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_HELD);
    CHECK(rec.events[0].data.key.id == KeyId::UP);

    const uint32_t afterFirst = 1000 + KeyboardDecoder::REPEAT_DELAY_MS;
    d.tick(afterFirst + KeyboardDecoder::REPEAT_INTERVAL_MS - 1);
    CHECK_EQ(rec.size(), 1);

    d.tick(afterFirst + KeyboardDecoder::REPEAT_INTERVAL_MS);
    CHECK_EQ(rec.size(), 2);
    CHECK(rec.events[1].type == events::EventType::KEY_HELD);
}

TEST(decoder_emits_one_repeat_for_a_late_tick_not_a_burst) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS + 5000);   // 5 seconds late

    CHECK_EQ(rec.size(), 1);
}

TEST(decoder_stops_repeating_on_release) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onReport(report(0, {}), 1100);
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS + 1000);

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_moves_repeat_to_the_most_recently_pressed_key) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onReport(report(0, {UP_USAGE, DOWN_USAGE}), 1100);
    rec.clear();

    d.tick(1100 + KeyboardDecoder::REPEAT_DELAY_MS);

    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].data.key.id == KeyId::DOWN);
}

TEST(decoder_stops_repeating_when_the_target_is_released_even_if_others_are_down) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onReport(report(0, {UP_USAGE, DOWN_USAGE}), 1100);
    d.onReport(report(0, {UP_USAGE}), 1200);       // release the repeat target
    rec.clear();

    d.tick(1200 + KeyboardDecoder::REPEAT_DELAY_MS + 1000);

    CHECK_EQ(rec.size(), 0);   // no fallback to the still-held UP
}

TEST(decoder_repeats_carry_live_modifiers) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onReport(report(mod::SHIFT, {UP_USAGE}), 1100);   // shift pressed mid-hold
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS);

    CHECK_EQ(rec.size(), 1);
    CHECK_EQ(rec.events[0].data.key.mods, mod::SHIFT);
}

TEST(decoder_repeat_survives_the_millisecond_rollover) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    const uint32_t nearMax = 0xFFFFFF00;
    d.onReport(report(0, {UP_USAGE}), nearMax);
    rec.clear();

    // nearMax + 400 wraps past 0xFFFFFFFF
    d.tick(static_cast<uint32_t>(nearMax + KeyboardDecoder::REPEAT_DELAY_MS));

    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_HELD);
}

TEST(decoder_releases_everything_on_disconnect) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE, A_USAGE}), 1000);
    rec.clear();

    d.onDisconnect();

    CHECK_EQ(rec.size(), 2);
    CHECK(rec.events[0].type == events::EventType::KEY_RELEASED);
    CHECK(rec.events[1].type == events::EventType::KEY_RELEASED);
}

TEST(decoder_stops_repeating_after_disconnect) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onDisconnect();
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS + 1000);

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_passes_through_keys_that_have_no_named_enumerator) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {0x49}), 1000);   // Insert - not in KeyId

    CHECK_EQ(rec.size(), 1);
    CHECK_EQ(static_cast<uint8_t>(rec.events[0].data.key.id), 0x49);
}
