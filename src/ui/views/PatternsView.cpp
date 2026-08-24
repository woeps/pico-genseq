#include "PatternsView.h"
#include "../state/Reducer.h"
#include "../Types.h"
#include <cstdio>

namespace ui {

PatternsView::PatternsView(hardware::LedMatrix& ledMatrix)
    : ledMatrix(ledMatrix) {}

void PatternsView::onEnter()
{
    printf("Entering Patterns View\n");
    ledMatrix.clear();
}

state::UIState PatternsView::handleEvent(const state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    state::UIState newState = state;

    switch (combo(event.data.key.id, event.data.key.mods)) {
        case combo(KeyId::UP): {
            int idx = newState.selectedPattern - GRID_COLS;
            if (idx < 0) idx = 0;
            state::setSelectedPattern(newState, static_cast<uint8_t>(idx));
            break;
        }
        case combo(KeyId::DOWN): {
            int idx = newState.selectedPattern + GRID_COLS;
            uint8_t maxIdx = newState.patternCount;
            if (idx > maxIdx) idx = maxIdx;
            state::setSelectedPattern(newState, static_cast<uint8_t>(idx));
            break;
        }
        case combo(KeyId::LEFT): {
            int idx = newState.selectedPattern - 1;
            if (idx < 0) idx = 0;
            state::setSelectedPattern(newState, static_cast<uint8_t>(idx));
            break;
        }
        case combo(KeyId::RIGHT): {
            int idx = newState.selectedPattern + 1;
            uint8_t maxIdx = newState.patternCount;
            if (idx > maxIdx) idx = maxIdx;
            state::setSelectedPattern(newState, static_cast<uint8_t>(idx));
            break;
        }
        case combo(KeyId::ENTER): {
            if (newState.selectedPattern == newState.patternCount) {
                state::addPattern(newState);
            } else {
                state::togglePatternActive(newState, newState.selectedPattern);
            }
            break;
        }
        default: break;
    }

    return newState;
}

void PatternsView::render(const state::UIState& state)
{
    ledMatrix.clear();
    ledMatrix.drawLabel("PTN", 0xFFFFEE00);

    uint8_t slotsToShow = state.patternCount + 1;
    if (slotsToShow > MAX_PATTERNS) slotsToShow = MAX_PATTERNS;

    for (uint8_t i = 0; i < slotsToShow; i++) {
        uint8_t col = i % GRID_COLS;
        uint8_t row = i / GRID_COLS;

        uint8_t x = GRID_START_X + col * (SQUARE_SIZE + GRID_GAP);
        uint8_t y = GRID_START_Y + row * (SQUARE_SIZE + GRID_GAP);

        uint32_t color;
        if (i == state.patternCount) {
            color = COLOR_NEW_SLOT;
        } else if (state.activePatterns & (1 << i)) {
            color = COLOR_ACTIVE;
        } else {
            color = COLOR_INACTIVE;
        }

        bool isNewSlot = (i == state.patternCount);
        uint8_t rowsToDraw = isNewSlot ? SQUARE_SIZE / 2 : SQUARE_SIZE;
        uint8_t startDy = isNewSlot ? SQUARE_SIZE / 2 : 0;
        for (uint8_t dy = startDy; dy < startDy + rowsToDraw; dy++) {
            for (uint8_t dx = 0; dx < SQUARE_SIZE; dx++) {
                ledMatrix.setPixel(x + dx, y + dy, color);
            }
        }

        if (i == state.selectedPattern) {
            if (isNewSlot) {
                ledMatrix.setPixel(x + SQUARE_SIZE - 1, y + SQUARE_SIZE - 1, COLOR_SELECTED);
            } else {
                ledMatrix.setPixel(x + SQUARE_SIZE - 1, y, COLOR_SELECTED);
            }
        }
    }
}

} // namespace ui
