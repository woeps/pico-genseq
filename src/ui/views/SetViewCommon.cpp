#include "SetViewCommon.h"

#include "../state/Reducer.h"
#include "../state/SetLibrary.h"
#include "../Types.h"

namespace ui {

namespace {

//                                       0xXXGGRRBB
constexpr uint32_t COLOR_SLOT_OCCUPIED = 0x00200000;
constexpr uint32_t COLOR_SLOT_SELECTED = 0x00FFFFFF;
constexpr uint32_t COLOR_USER = 0x0000FF00;
constexpr uint32_t COLOR_CANDIDATE = 0x00005050;
constexpr uint32_t COLOR_LABEL = 0xFFFFEE00;
constexpr uint32_t COLOR_VALUE = 0xFFFF0011;
constexpr uint8_t ROW_USERS = 14;
constexpr uint8_t ROW_SLOTS = 15;

uint8_t selectedSlot(const state::UIState& s, state::SetType type) {
    switch (type) {
        case state::SetType::GATE: return s.selectedGateSlot;
        case state::SetType::NOTE: return s.selectedNoteSlot;
        default:                   return s.selectedVelocitySlot;
    }
}

void drawPicker(hardware::LedMatrix& matrix, const state::UIState& s) {
    matrix.clear();
    matrix.drawLabel("gto", COLOR_LABEL);
    // Pattern numbers count from 1 on the display (pattern 1 is the top-left square).
    matrix.drawNumber(s.gotoPicker.cursor + 1, COLOR_VALUE);
    for (uint8_t p = 0; p < state::MAX_PATTERNS; ++p) {
        if (!((s.gotoPicker.candidates >> p) & 1u)) continue;
        matrix.setPixel(p, ROW_USERS, p == s.gotoPicker.cursor ? COLOR_SLOT_SELECTED : COLOR_CANDIDATE);
    }
}

void drawNotice(hardware::LedMatrix& matrix, state::SetNotice notice) {
    switch (notice) {
        case state::SetNotice::LIBRARY_FULL: matrix.drawValueText("ful", COLOR_VALUE); break;
        case state::SetNotice::IN_USE:       matrix.drawValueText("use", COLOR_VALUE); break;
        case state::SetNotice::LAST_SET:     matrix.drawValueText("lst", COLOR_VALUE); break;
        case state::SetNotice::UNUSED:       matrix.drawValueText("unu", COLOR_VALUE); break;
        case state::SetNotice::NONE:         break;
    }
}

}  // namespace

bool handleSetLibraryKey(state::UIState& state, state::SetType type, const events::Event& event) {
    if (event.type == events::EventType::KEY_PRESSED) {
        state.setNotice = state::SetNotice::NONE;  // any press dismisses a notice
    }
    const bool pressed = event.type == events::EventType::KEY_PRESSED;
    const bool movable = pressed || event.type == events::EventType::KEY_HELD;
    switch (combo(event.data.key.id, event.data.key.mods)) {
        case combo(KeyId::LEFT, mod::CTRL):
            if (movable) state::stepSetSlot(state, type, -1);
            return true;
        case combo(KeyId::RIGHT, mod::CTRL):
            if (movable) state::stepSetSlot(state, type, +1);
            return true;
        case combo(KeyId::N, mod::CTRL):
            if (pressed) state::createSetInView(state, type);
            return true;
        case combo(KeyId::D, mod::CTRL):
            if (pressed) state::copySetInView(state, type);
            return true;
        case combo(KeyId::DELETE_KEY, mod::CTRL):
            if (pressed) state::deleteSetInView(state, type);
            return true;
        default:
            return false;
    }
}

void drawSetOverlay(hardware::LedMatrix& matrix, const state::UIState& s, state::SetType type) {
    if (s.gotoPicker.active) {
        drawPicker(matrix, s);
        return;
    }
    const uint8_t slot = selectedSlot(s, type);
    const uint16_t occupied = state::occupiedMask(s, type);
    for (uint8_t i = 0; i < state::SET_LIBRARY_SIZE; ++i) {
        if (!((occupied >> i) & 1u)) continue;
        matrix.setPixel(i, ROW_SLOTS, i == slot ? COLOR_SLOT_SELECTED : COLOR_SLOT_OCCUPIED);
    }
    const uint16_t users = state::usersOf(s, type, slot);
    for (uint8_t p = 0; p < state::MAX_PATTERNS; ++p) {
        if ((users >> p) & 1u) matrix.setPixel(p, ROW_USERS, COLOR_USER);
    }
    drawNotice(matrix, s.setNotice);
}

}  // namespace ui
