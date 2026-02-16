#pragma once

namespace ui {

// Button ID enum for identifying different buttons
enum class ButtonId {
    BUTTON_A,
    BUTTON_B,
    BUTTON_C,
    BUTTON_D,
    BUTTON_E,
    BUTTON_F
};

static constexpr int BUTTON_COUNT = 6;

// Potentiometer ID enum for identifying different potentiometers
enum class PotId {
    POT_A
};

static constexpr int POT_COUNT = 1;

} // namespace ui
