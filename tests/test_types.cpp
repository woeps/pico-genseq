#include "framework.h"
#include "ui/Types.h"
#include "ui/KeyNames.h"

using namespace ui;

TEST(combo_packs_key_in_low_byte_and_mods_in_high_byte) {
    CHECK_EQ(combo(KeyId::UP), 0x0052);
    CHECK_EQ(combo(KeyId::UP, mod::SHIFT), 0x0252);
    CHECK_EQ(combo(KeyId::F1), 0x003A);
    CHECK_EQ(combo(KeyId::SPACE), 0x002C);
}

TEST(combo_distinguishes_modified_from_bare) {
    CHECK(combo(KeyId::UP) != combo(KeyId::UP, mod::SHIFT));
    CHECK(combo(KeyId::UP, mod::SHIFT) != combo(KeyId::UP, mod::CTRL));
}

TEST(combo_is_usable_as_a_switch_label) {
    switch (combo(KeyId::DOWN, mod::SHIFT)) {
        case combo(KeyId::DOWN):             CHECK(false); break;
        case combo(KeyId::DOWN, mod::SHIFT): CHECK(true);  break;
        default:                             CHECK(false); break;
    }
}

TEST(key_ids_are_hid_usage_codes) {
    CHECK_EQ(static_cast<uint8_t>(KeyId::A), 0x04);
    CHECK_EQ(static_cast<uint8_t>(KeyId::SPACE), 0x2C);
    CHECK_EQ(static_cast<uint8_t>(KeyId::F1), 0x3A);
    CHECK_EQ(static_cast<uint8_t>(KeyId::F12), 0x45);
    CHECK_EQ(static_cast<uint8_t>(KeyId::UP), 0x52);
}

TEST(function_keys_are_contiguous) {
    CHECK_EQ(static_cast<uint8_t>(KeyId::F12) - static_cast<uint8_t>(KeyId::F1), 11);
}

TEST(toName_returns_lowercase_names) {
    CHECK_STREQ(toName(KeyId::A), "a");
    CHECK_STREQ(toName(KeyId::SPACE), "space");
    CHECK_STREQ(toName(KeyId::F1), "f1");
    CHECK_STREQ(toName(KeyId::F12), "f12");
    CHECK_STREQ(toName(KeyId::UP), "up");
    CHECK_STREQ(toName(KeyId::NUM_0), "0");
}

TEST(toName_returns_question_mark_for_unnamed_keys) {
    CHECK_STREQ(toName(static_cast<KeyId>(0xB0)), "?");
    CHECK_STREQ(toName(KeyId::NONE), "?");
}
