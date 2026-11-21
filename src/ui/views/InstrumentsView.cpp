#include "InstrumentsView.h"
#include "../state/Instruments.h"
#include "../state/Reducer.h"
#include "../Types.h"

namespace ui {

namespace {

// Half brightness of every colour byte (muted Instruments are drawn dormant).
uint32_t dimmed(uint32_t color) {
    return (color >> 1) & 0x7F7F7F7Fu;
}

}  // namespace

InstrumentsView::InstrumentsView(hardware::LedMatrix& ledMatrix) : ledMatrix(ledMatrix) {}

void InstrumentsView::onEnter()
{
    ledMatrix.clear();
}

void InstrumentsView::handleEvent(state::UIState& state, const events::Event& event)
{
    const bool pressed = event.type == events::EventType::KEY_PRESSED;
    const bool repeat = pressed || event.type == events::EventType::KEY_HELD;
    if (!repeat) return;

    const uint8_t slot = state.selectedInstrument;
    const uint8_t cursor = state.instrumentCursor;

    switch (combo(event.data.key.id, event.data.key.mods)) {
        case combo(KeyId::LEFT):  state.instrumentNotice = state::InstrumentNotice::NONE; state::moveInstrumentCursor(state, -1, 0); break;
        case combo(KeyId::RIGHT): state.instrumentNotice = state::InstrumentNotice::NONE; state::moveInstrumentCursor(state, +1, 0); break;
        case combo(KeyId::UP):    state.instrumentNotice = state::InstrumentNotice::NONE; state::moveInstrumentCursor(state, 0, -1); break;
        case combo(KeyId::DOWN):  state.instrumentNotice = state::InstrumentNotice::NONE; state::moveInstrumentCursor(state, 0, +1); break;
        case combo(KeyId::LEFT, mod::CTRL):
            state.instrumentNotice = state::InstrumentNotice::NONE;
            state::selectInstrumentStep(state, -1);
            break;
        case combo(KeyId::RIGHT, mod::CTRL):
            state.instrumentNotice = state::InstrumentNotice::NONE;
            state::selectInstrumentStep(state, +1);
            break;
        case combo(KeyId::LEFT, mod::SHIFT):  state::changeInstrumentChannel(state, slot, -1); break;
        case combo(KeyId::RIGHT, mod::SHIFT): state::changeInstrumentChannel(state, slot, +1); break;
        case combo(KeyId::ENTER):
            if (pressed) state::toggleInstrumentPattern(state, slot, cursor);
            break;
        case combo(KeyId::E):
            if (pressed) state::toggleInstrumentRoute(state, slot, cursor);
            break;
        case combo(KeyId::M):
            if (pressed) state::toggleInstrumentMute(state, slot);
            break;
        case combo(KeyId::N, mod::CTRL):
            if (pressed) state::createInstrument(state);
            break;
        case combo(KeyId::DELETE_KEY):
            if (pressed) state::deleteInstrument(state, slot);
            break;
        default: break;
    }
}

void InstrumentsView::render(const state::UIState& state)
{
    const state::InstrumentConfig& in = state.instruments[state.selectedInstrument];

    // Label "cNN": the channel of the selected Instrument.
    char label[4] = {'c', static_cast<char>('0' + in.channel / 10),
                     static_cast<char>('0' + in.channel % 10), '\0'};
    ledMatrix.drawLabel(label, in.muted ? COLOR_LABEL_MUTED : COLOR_LABEL);

    // Pattern grid: all 15 cells every time, so a state change never leaves a stale pixel.
    for (uint8_t i = 0; i < state::MAX_PATTERNS; ++i) {
        const uint8_t x = (i % GRID_COLS) * (SQUARE_SIZE + GRID_GAP);
        const uint8_t y = GRID_START_Y + (i / GRID_COLS) * (SQUARE_SIZE + GRID_GAP);
        const bool exists = i < state.patternCount;
        const bool member = exists && ((in.members >> i) & 1u);
        const bool routeOn = member && ((in.enabled >> i) & 1u);
        const bool active = ((state.activePatterns >> i) & 1u) != 0;

        uint32_t top = 0, bottom = 0;
        if (!exists) {
            top = bottom = 0;
        } else if (!member) {
            top = bottom = COLOR_NOT_MEMBER;
        } else if (!routeOn) {
            top = COLOR_ROUTE_OFF;  // bottom row dark: the route is switched off
        } else {
            top = bottom = active ? COLOR_SOUNDING : COLOR_PATTERN_OFF;
        }
        if (in.muted) { top = dimmed(top); bottom = dimmed(bottom); }

        ledMatrix.setPixel(x, y, top);
        ledMatrix.setPixel(x + 1, y, top);
        ledMatrix.setPixel(x, y + 1, bottom);
        ledMatrix.setPixel(x + 1, y + 1, bottom);

        // Cursor: two white pixels in the gap row under the cell.
        const uint32_t cursor = (i == state.instrumentCursor) ? COLOR_CURSOR : 0;
        ledMatrix.setPixel(x, y + SQUARE_SIZE, cursor);
        ledMatrix.setPixel(x + 1, y + SQUARE_SIZE, cursor);
    }

    // Instrument strip.
    const bool notice = state.instrumentNotice != state::InstrumentNotice::NONE;
    for (uint8_t s = 0; s < state::MAX_INSTRUMENTS; ++s) {
        uint32_t color = 0;
        if ((state.instrumentOccupied >> s) & 1u) {
            if (s == state.selectedInstrument) color = COLOR_STRIP_SELECTED;
            else color = state.instruments[s].muted ? COLOR_STRIP_MUTED : COLOR_STRIP_OCCUPIED;
        }
        if (notice) color = COLOR_NOTICE;
        ledMatrix.setPixel(s, STRIP_ROW, color);
    }
}

} // namespace ui
