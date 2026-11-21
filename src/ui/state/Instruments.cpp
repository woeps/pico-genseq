#include "Instruments.h"

namespace ui::state {

namespace {

constexpr uint8_t GRID_COLS = 5;
constexpr uint8_t GRID_ROWS = 3;

uint16_t patternMask(const UIState& s) {
    return s.patternCount >= 16 ? static_cast<uint16_t>(0xFFFF)
                                : static_cast<uint16_t>((1u << s.patternCount) - 1u);
}

bool occupied(const UIState& s, uint8_t slot) {
    return slot < MAX_INSTRUMENTS && ((s.instrumentOccupied >> slot) & 1u);
}

bool channelUsedByOther(const UIState& s, uint8_t slot, uint8_t channel) {
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; ++i) {
        if (i != slot && occupied(s, i) && s.instruments[i].channel == channel) return true;
    }
    return false;
}

uint8_t lowestOccupiedSlot(const UIState& s) {
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; ++i) {
        if (occupied(s, i)) return i;
    }
    return 0;
}

}  // namespace

uint16_t effectiveMask(const UIState& state, uint8_t slot) {
    if (!occupied(state, slot)) return 0;
    const InstrumentConfig& in = state.instruments[slot];
    if (in.muted) return 0;
    return static_cast<uint16_t>(in.members & patternMask(state));
}

uint8_t protocolChannelOf(const UIState& state, uint8_t slot) {
    if (slot >= MAX_INSTRUMENTS) return 0;
    const uint8_t ch = state.instruments[slot].channel;
    return ch >= 1 && ch <= 16 ? static_cast<uint8_t>(ch - 1) : 0;
}

uint8_t lowestFreeChannel(const UIState& state) {
    for (uint8_t ch = 1; ch <= 16; ++ch) {
        if (!channelUsedByOther(state, MAX_INSTRUMENTS, ch)) return ch;
    }
    return 0;
}

uint16_t instrumentsOf(const UIState& state, uint8_t pattern) {
    uint16_t m = 0;
    for (uint8_t s = 0; s < MAX_INSTRUMENTS; ++s) {
        if (occupied(state, s) && ((state.instruments[s].members >> pattern) & 1u)) {
            m = static_cast<uint16_t>(m | (1u << s));
        }
    }
    return m;
}

bool createInstrumentSlot(UIState& state, uint8_t& newSlot) {
    state.instrumentNotice = InstrumentNotice::NONE;
    int slot = -1;
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; ++i) {
        if (!occupied(state, i)) { slot = i; break; }
    }
    const uint8_t channel = lowestFreeChannel(state);
    if (slot < 0 || channel == 0) {
        state.instrumentNotice = InstrumentNotice::FULL;
        return false;
    }
    state.instruments[slot] = InstrumentConfig{channel, false, 0};
    state.instrumentOccupied = static_cast<uint16_t>(state.instrumentOccupied | (1u << slot));
    state.selectedInstrument = static_cast<uint8_t>(slot);
    newSlot = static_cast<uint8_t>(slot);
    return true;
}

bool deleteInstrumentSlot(UIState& state, uint8_t slot) {
    state.instrumentNotice = InstrumentNotice::NONE;
    if (!occupied(state, slot)) return false;
    if ((state.instrumentOccupied & (state.instrumentOccupied - 1u)) == 0) {
        state.instrumentNotice = InstrumentNotice::LAST_INSTRUMENT;
        return false;
    }
    state.instruments[slot] = InstrumentConfig{};
    state.instrumentOccupied = static_cast<uint16_t>(state.instrumentOccupied & ~(1u << slot));
    if (state.selectedInstrument == slot || !occupied(state, state.selectedInstrument)) {
        state.selectedInstrument = lowestOccupiedSlot(state);
    }
    return true;
}

bool stepInstrumentChannel(UIState& state, uint8_t slot, int direction) {
    state.instrumentNotice = InstrumentNotice::NONE;
    if (!occupied(state, slot) || direction == 0) return false;
    const int step = direction > 0 ? 1 : -1;
    for (int ch = state.instruments[slot].channel + step; ch >= 1 && ch <= 16; ch += step) {
        if (!channelUsedByOther(state, slot, static_cast<uint8_t>(ch))) {
            state.instruments[slot].channel = static_cast<uint8_t>(ch);
            return true;
        }
    }
    state.instrumentNotice = InstrumentNotice::NO_FREE_CHANNEL;
    return false;
}

bool toggleInstrumentMuteFlag(UIState& state, uint8_t slot) {
    state.instrumentNotice = InstrumentNotice::NONE;
    if (!occupied(state, slot)) return false;
    state.instruments[slot].muted = !state.instruments[slot].muted;
    return true;
}

bool toggleInstrumentMember(UIState& state, uint8_t slot, uint8_t p) {
    state.instrumentNotice = InstrumentNotice::NONE;
    if (!occupied(state, slot) || p >= state.patternCount) return false;
    InstrumentConfig& in = state.instruments[slot];
    in.members = static_cast<uint16_t>(in.members ^ (1u << p));
    return true;
}

void assignPatternToInstrument(UIState& state, uint8_t slot, uint8_t p) {
    if (!occupied(state, slot) || p >= MAX_PATTERNS) return;
    const uint16_t bit = static_cast<uint16_t>(1u << p);
    state.instruments[slot].members = static_cast<uint16_t>(state.instruments[slot].members | bit);
}

void removePatternFromInstruments(UIState& state, uint8_t index) {
    for (uint8_t s = 0; s < MAX_INSTRUMENTS; ++s) {
        state.instruments[s].members = common::removePatternBit(state.instruments[s].members, index);
    }
}

uint8_t selectInstrumentStep(UIState& state, int direction) {
    const int step = direction >= 0 ? 1 : -1;
    for (int s = state.selectedInstrument + step; s >= 0 && s < MAX_INSTRUMENTS; s += step) {
        if (occupied(state, static_cast<uint8_t>(s))) {
            state.selectedInstrument = static_cast<uint8_t>(s);
            break;
        }
    }
    return state.selectedInstrument;
}

void moveInstrumentCursor(UIState& state, int dx, int dy) {
    int col = state.instrumentCursor % GRID_COLS + dx;
    int row = state.instrumentCursor / GRID_COLS + dy;
    if (col < 0) col = 0;
    if (col >= GRID_COLS) col = GRID_COLS - 1;
    if (row < 0) row = 0;
    if (row >= GRID_ROWS) row = GRID_ROWS - 1;
    state.instrumentCursor = static_cast<uint8_t>(row * GRID_COLS + col);
}

bool validateInstruments(const UIState& state) {
    if (state.instrumentOccupied == 0) return false;
    if (state.instrumentOccupied >> MAX_INSTRUMENTS) return false;
    if (!occupied(state, state.selectedInstrument)) return false;
    const uint16_t valid = patternMask(state);
    for (uint8_t i = 0; i < MAX_INSTRUMENTS; ++i) {
        if (!occupied(state, i)) continue;
        const InstrumentConfig& in = state.instruments[i];
        if (in.channel < 1 || in.channel > 16) return false;
        if (channelUsedByOther(state, i, in.channel)) return false;
        if (in.members & ~valid) return false;
    }
    return true;
}

} // namespace ui::state
