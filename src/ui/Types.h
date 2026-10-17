#pragma once

#include <cstdint>

namespace ui {

// Values ARE HID usage codes - the decoder casts straight from the report.
enum class KeyId : uint8_t {
    NONE = 0x00,

    A = 0x04, B = 0x05, C = 0x06, D = 0x07, E = 0x08, F = 0x09,
    G = 0x0A, H = 0x0B, I = 0x0C, J = 0x0D, K = 0x0E, L = 0x0F,
    M = 0x10, N = 0x11, O = 0x12, P = 0x13, Q = 0x14, R = 0x15,
    S = 0x16, T = 0x17, U = 0x18, V = 0x19, W = 0x1A, X = 0x1B,
    Y = 0x1C, Z = 0x1D,

    NUM_1 = 0x1E, NUM_2 = 0x1F, NUM_3 = 0x20, NUM_4 = 0x21, NUM_5 = 0x22,
    NUM_6 = 0x23, NUM_7 = 0x24, NUM_8 = 0x25, NUM_9 = 0x26, NUM_0 = 0x27,

    ENTER = 0x28, ESCAPE = 0x29, BACKSPACE = 0x2A, TAB = 0x2B,
    SPACE = 0x2C, MINUS = 0x2D, EQUAL = 0x2E,

    F1 = 0x3A, F2  = 0x3B, F3  = 0x3C, F4  = 0x3D, F5  = 0x3E, F6  = 0x3F,
    F7 = 0x40, F8  = 0x41, F9  = 0x42, F10 = 0x43, F11 = 0x44, F12 = 0x45,

    DELETE_KEY = 0x4C,
    RIGHT = 0x4F, LEFT = 0x50, DOWN = 0x51, UP = 0x52,
};

// Mirrors the low nibble of the HID modifier byte. Right-hand modifiers
// occupy the high nibble and are folded onto these same bits by the decoder.
namespace mod {
    constexpr uint8_t NONE  = 0;
    constexpr uint8_t CTRL  = 1 << 0;
    constexpr uint8_t SHIFT = 1 << 1;
    constexpr uint8_t ALT   = 1 << 2;
    constexpr uint8_t GUI   = 1 << 3;
}

// Packs a key and its modifiers into one value usable as a switch label.
constexpr uint16_t combo(KeyId id, uint8_t mods = mod::NONE) {
    return static_cast<uint16_t>((static_cast<uint16_t>(mods) << 8) |
                                 static_cast<uint8_t>(id));
}

} // namespace ui
