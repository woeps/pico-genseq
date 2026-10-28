#include "SettingsView.h"
#include "../state/Reducer.h"
#include "../Types.h"
#include <cstdio>

namespace ui {

namespace {

// The 3-character matrix labels for the two fields (drawLabel/drawNote take
// exactly a 4-byte array: three characters plus a trailing byte).
constexpr char kBpmLabel[4] = {'b', 'P', 'M', '\0'};

// Pitch-class spelling for the root display (C..B, sharps), mirroring the
// sibling views' 12-note table.
constexpr char ROOT_LETTERS[SettingsView::kRootCount] = {
    'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B',
};
constexpr bool ROOT_SHARPS[SettingsView::kRootCount] = {
    false, true, false, true, false, false, true, false, true, false, true, false,
};

// Euclidean modulo (the C++ % of a negative value is negative): maps any
// `value` into [0, modulus). Bounded O(1) arithmetic, no loops.
int wrapMod(int value, int modulus) {
    const int wrapped = value % modulus;
    return wrapped < 0 ? wrapped + modulus : wrapped;
}

} // namespace

SettingsView::SettingsView(hardware::Led& led, hardware::LedMatrix& ledMatrix)
    : led(led), ledMatrix(ledMatrix) {}

void SettingsView::onEnter()
{
    printf("Entering Settings View\n");
    ledMatrix.clear();
}

state::UIState SettingsView::handleEvent(const state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    state::UIState newState = state;
    const bool isPressed = event.type == events::EventType::KEY_PRESSED;
    // SHIFT is handled as the coarse flag below, so strip it from the combo
    // match (sibling-view convention).
    const uint8_t comboMods = event.data.key.mods & ~mod::SHIFT;
    const bool coarse = (event.data.key.mods & mod::SHIFT) != 0;

    switch (combo(event.data.key.id, comboMods)) {
        case combo(KeyId::ENTER):
            // Cycle the edited field (bPM <-> Scale); held repeats ignored.
            if (isPressed) cycleField();
            break;
        case combo(KeyId::UP):
        case combo(KeyId::DOWN): {
            const int direction = event.data.key.id == KeyId::UP ? 1 : -1;
            if (field == Field::BPM) {
                const int step = coarse ? direction * 10 : direction;
                state::setBpm(newState, state.bpm + step);
            } else {
                // Field::Scale: UP/DOWN adjust the root pitch class with wrap.
                const int step = coarse ? direction * kCoarseStep : direction;
                const uint8_t root = static_cast<uint8_t>(wrapMod(
                    static_cast<int>(state.scaleRoot) + step, kRootCount));
                // Every change dispatches ONE SCALE_SET through the reducer.
                state::setScale(newState, root, state.scaleTemplateIndex);
            }
            break;
        }
        case combo(KeyId::LEFT):
        case combo(KeyId::RIGHT):
            // Field::Scale only: LEFT/RIGHT adjust the template index with
            // wrap across the built-in templates. On Field::BPM the
            // horizontal keys are unbound.
            if (field == Field::SCALE) {
                const int direction = event.data.key.id == KeyId::RIGHT ? 1 : -1;
                const int step = coarse ? direction * kCoarseStep : direction;
                const uint8_t templateIdx = static_cast<uint8_t>(wrapMod(
                    static_cast<int>(state.scaleTemplateIndex) + step,
                    scaleTemplateCount()));
                state::setScale(newState, state.scaleRoot, templateIdx);
            }
            break;
        default: break;
    }

    return newState;
}

void SettingsView::render(const state::UIState& state)
{
    if (field == Field::BPM) {
        ledMatrix.drawLabel(kBpmLabel, 0xFF00FF33);
        ledMatrix.drawNumber(state.bpm, 0xFFFF0011);
    } else {
        // The combined Scale field: the scale NAME (from ScaleTemplates) in
        // the 3-char label region, the ROOT as a note name (C..B) on the
        // note region.
        char label[4];
        scaleNameLabel(state.scaleTemplateIndex, label);
        ledMatrix.drawLabel(label, 0xFF00FF33);
        char note[4];
        rootNoteName(state.scaleRoot, note);
        ledMatrix.drawNote(note, 0xFFFF0011);
    }
}

// --- field selection ---------------------------------------------------------

void SettingsView::selectField(Field field) { this->field = field; }

SettingsView::Field SettingsView::selectedField() const { return field; }

void SettingsView::cycleField() {
    field = (field == Field::BPM) ? Field::SCALE : Field::BPM;
}

// --- scale rendering helpers ---------------------------------------------------

int SettingsView::scaleTemplateCount() { return common::scaleTemplateCount(); }

const char* SettingsView::scaleNameAt(int index) {
    // common::scaleTemplate clamps both ends, so the accessor is total.
    return common::scaleTemplate(index).name;
}

void SettingsView::scaleNameLabel(int index, char (&out)[4]) {
    // The verbatim ScaleTemplates name, truncated to the 3-char matrix label
    // slot (never invented). Blanks pad names shorter than three characters.
    const char* name = common::scaleTemplate(index).name;
    out[0] = name[0] != '\0' ? name[0] : ' ';
    out[1] = name[1] != '\0' ? name[1] : ' ';
    out[2] = name[2] != '\0' ? name[2] : ' ';
    out[3] = '\0';
}

void SettingsView::rootNoteName(int root, char (&out)[4]) {
    if (root < 0) root = 0;
    if (root >= kRootCount) root = kRootCount - 1;
    out[0] = ROOT_LETTERS[root];
    out[1] = ROOT_SHARPS[root] ? '#' : '-';
    out[2] = ' ';
    out[3] = '\0';
}

} // namespace ui
