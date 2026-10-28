// Per-pattern root note + scale selection view (generative-engine-expansion
// task 19, FR4; ui-integration T8). See ScaleSelectionView.h for the design;
// this translation unit holds the wrap arithmetic, the PATTERN_SCALE_SET
// command write-through and the key-event front end.

#include "ScaleSelectionView.h"

#include "../../commands/command.h"
#include "../Types.h"

#include <cstdio>

namespace ui {

namespace {

// The 3-character matrix labels for the two fields (drawLabel/drawNote take
// exactly a 4-byte array: three characters plus a trailing byte).
constexpr char kRootLabel[4] = {'R', 'o', 't', '\0'};
constexpr char kScaleLabel[4] = {'S', 'c', 'l', '\0'};

// Pitch-class spelling for the root display (C..B, sharps), mirroring the
// PitchSetView convention (lower/upper case aside, the same 12-note table).
constexpr char ROOT_LETTERS[ScaleSelectionView::kRootCount] = {
    'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B',
};
constexpr bool ROOT_SHARPS[ScaleSelectionView::kRootCount] = {
    false, true, false, true, false, false, true, false, true, false, true, false,
};

// Fill the 4-byte note-name slot the matrix driver expects (three visible
// characters plus a trailing blank). `root` is a pitch class 0-11.
void rootNoteName(int root, char (&out)[4]) {
    if (root < 0) root = 0;
    if (root >= ScaleSelectionView::kRootCount) {
        root = ScaleSelectionView::kRootCount - 1;
    }
    out[0] = ROOT_LETTERS[root];
    out[1] = ROOT_SHARPS[root] ? '#' : '-';
    out[2] = ' ';
    out[3] = '\0';
}

// Euclidean modulo (the C++ % of a negative value is negative): maps any
// `value` into [0, modulus). Bounded O(1) arithmetic, no loops.
int wrapMod(int value, int modulus) {
    const int wrapped = value % modulus;
    return wrapped < 0 ? wrapped + modulus : wrapped;
}

}  // namespace

ScaleSelectionView::ScaleSelectionView(hardware::LedMatrix& ledMatrix)
    : ledMatrix_(ledMatrix) {}

// --- field selection -----------------------------------------------------------

void ScaleSelectionView::selectField(Field field) { field_ = field; }

ScaleSelectionView::Field ScaleSelectionView::selectedField() const {
    return field_;
}

void ScaleSelectionView::cycleField() {
    field_ = (field_ == Field::Root) ? Field::Scale : Field::Root;
}

// --- built-in scale templates ----------------------------------------------------

int ScaleSelectionView::scaleCount() { return common::scaleTemplateCount(); }

const char* ScaleSelectionView::scaleNameAt(int index) {
    // common::scaleTemplate clamps both ends, so the accessor is total.
    return common::scaleTemplate(index).name;
}

// --- per-pattern values ------------------------------------------------------------

int ScaleSelectionView::rootValue(uint8_t patternIndex) const {
    return validPattern(patternIndex) ? roots_[patternIndex] : 0;
}

int ScaleSelectionView::scaleIndexValue(uint8_t patternIndex) const {
    return validPattern(patternIndex) ? templates_[patternIndex] : 0;
}

// --- selections --------------------------------------------------------------------

bool ScaleSelectionView::setRoot(uint8_t patternIndex, int root) {
    if (!validPattern(patternIndex)) return false;
    if (root < 0 || root >= kRootCount) return false;  // a root is 0-11, else nothing
    roots_[patternIndex] = static_cast<uint8_t>(root);
    postPatternScale(patternIndex);
    return true;
}

bool ScaleSelectionView::setScaleIndex(uint8_t patternIndex, int index) {
    if (!validPattern(patternIndex)) return false;
    if (index < 0 || index >= scaleCount()) return false;  // real templates only
    templates_[patternIndex] = static_cast<uint8_t>(index);
    postPatternScale(patternIndex);
    return true;
}

bool ScaleSelectionView::cycleRoot(uint8_t patternIndex, int delta) {
    if (!validPattern(patternIndex)) return false;
    roots_[patternIndex] = static_cast<uint8_t>(wrapMod(
        static_cast<int>(roots_[patternIndex]) + delta, kRootCount));
    postPatternScale(patternIndex);
    return true;
}

bool ScaleSelectionView::cycleScaleIndex(uint8_t patternIndex, int delta) {
    if (!validPattern(patternIndex)) return false;
    templates_[patternIndex] = static_cast<uint8_t>(wrapMod(
        static_cast<int>(templates_[patternIndex]) + delta, scaleCount()));
    postPatternScale(patternIndex);
    return true;
}

// --- IView ---------------------------------------------------------------------------

void ScaleSelectionView::onEnter() {
    std::printf("Entering Scale Selection View\n");
    ledMatrix_.clear();
}

state::UIState ScaleSelectionView::handleEvent(const state::UIState& state,
                                               const events::Event& event) {
    // The edited values live in the view's local UI cache and cross the
    // cores only as PATTERN_SCALE_SET commands — not in the UIState (which
    // carries no scale fields) — so every path here returns the state
    // unchanged except the ESCAPE back-out to the parent view.
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    const uint8_t pattern = state.selectedPattern;
    const bool coarse = (event.data.key.mods & mod::SHIFT) != 0;
    const int step = coarse ? kCoarseStep : 1;
    switch (combo(event.data.key.id, event.data.key.mods & ~mod::SHIFT)) {
        case combo(KeyId::ESCAPE):
            // Back out to the PATTERNS view (FR11: ESCAPE returns to the
            // appropriate parent). Held repeats are ignored, like the
            // sibling views.
            if (event.type == events::EventType::KEY_PRESSED) {
                state::UIState next = state;
                next.currentView = state::ViewId::PATTERNS;
                return next;
            }
            break;
        case combo(KeyId::LEFT):
        case combo(KeyId::RIGHT):
            // Toggle the edited field (two fields: both directions meet).
            cycleField();
            break;
        case combo(KeyId::UP):
            if (field_ == Field::Root) {
                cycleRoot(pattern, +step);
            } else {
                cycleScaleIndex(pattern, +step);
            }
            break;
        case combo(KeyId::DOWN):
            if (field_ == Field::Root) {
                cycleRoot(pattern, -step);
            } else {
                cycleScaleIndex(pattern, -step);
            }
            break;
        default:
            break;
    }
    return state;
}

void ScaleSelectionView::render(const state::UIState& state) {
    ledMatrix_.clear();
    const uint8_t pattern = state.selectedPattern;
    if (field_ == Field::Root) {
        ledMatrix_.drawLabel(kRootLabel, 0xFFFFEE00);
        char note[4];
        rootNoteName(rootValue(pattern), note);
        ledMatrix_.drawNote(note, 0xFFFF0011);
    } else {
        ledMatrix_.drawLabel(kScaleLabel, 0xFFFFEE00);
        // The 16x16 matrix shows the template INDEX; the full names stay
        // available through scaleNameAt (e.g. for a richer display).
        ledMatrix_.drawNumber(scaleIndexValue(pattern), 0xFFFF0011);
    }
}

// --- private helpers -----------------------------------------------------------------

bool ScaleSelectionView::validPattern(uint8_t patternIndex) {
    return patternIndex < common::PATTERN_CAPACITY;
}

void ScaleSelectionView::postPatternScale(uint8_t patternIndex) {
    // The view's ONLY cross-core write path: the pattern's full cached pair
    // rides ONE command word to the sequencer core (the sequencer applies
    // it as a plain bus BASE write into its own working state).
    commands::sendPatternScaleCommand(patternIndex, roots_[patternIndex],
                                      templates_[patternIndex]);
}

}  // namespace ui
