#pragma once

#include <cstdint>

#include "IView.h"
#include "../hardware/LedMatrix.h"
#include "../../common/PatternId.h"
#include "../../common/ScaleTemplates.h"

namespace ui {

// Per-pattern root note + scale selection view
// (generative-engine-expansion task 19, FR4; ui-integration T8).
//
// The view selects ONE pattern's scale constraint: a root pitch class
// (0-11, C..B) and an index into the fixed built-in template table
// (common::SCALE_TEMPLATES — the FR3 set; user-definable scales are out of
// scope). The selected pair is what constrains pitch variation for that
// pattern: the scale-aware pitch generator and the pitch mutation operators
// read it on the sequencer core through its own working state.
//
// Edits ride the COMMAND QUEUE (ui-integration T8): a committed selection
// posts ONE PATTERN_SCALE_SET command carrying the pattern's full
// (root, template) pair to the sequencer core — the view NEVER touches the
// ParameterBus. Core0 keeps its OWN local UI cache of the per-pattern
// values (seeded with the factory defaults root 0 / template 0, C Major —
// the same defaults the task-16 parameter table gives ParamId::ScaleRoot /
// ParamId::ScaleTemplate), so reads and relative adjustments stay pure
// Core0 state — no data structure is shared between the cores. The
// sequencer applies the command as a plain bus BASE write into the live
// per-pattern generative working state; no modulation overlay is ever armed
// (overlays belong to the modulation layer alone).
//
// Selection state is view-local by design: UIState carries no scale fields
// (adding them is outside this task's declared scope), so which FIELD the
// keys adjust (root / scale) lives in this view while the edited VALUES
// cross to the sequencer core only as commands. handleEvent therefore
// returns the UIState unchanged except the ESCAPE back-out to the parent
// view.
//
// Key mapping (documented, follows the sibling view conventions):
//   LEFT / RIGHT   - toggle the edited field (root <-> scale)
//   UP / DOWN      - cycle the selected field by 1 with WRAP (SHIFT: coarse 5)
//   ESCAPE         - return to the PATTERNS parent view (FR11)
//
// Global constraints honored here: no heap allocation, no unbounded loops
// (every operation is O(1) arithmetic or a compile-time-bounded table read),
// fixed-capacity storage (one root + one template byte per pattern slot),
// built-in templates only. The view never touches the tick path and never
// shares state with the sequencer core — commands only.
class ScaleSelectionView : public IView {
public:
    // Root pitch classes: C through B (FR4 root note).
    static constexpr int kRootCount = 12;
    // The coarse key step (SHIFT+UP/DOWN), matching the sibling views.
    static constexpr int kCoarseStep = 5;

    // Which of the view's two fields UP/DOWN cycle.
    enum class Field : uint8_t {
        Root = 0,   // the root pitch class
        Scale = 1,  // the scale template index
    };

    explicit ScaleSelectionView(hardware::LedMatrix& ledMatrix);

    // --- field selection ---------------------------------------------------
    // Select which field UP/DOWN cycle. Any Field value is one of the two,
    // so the selection is total (a corrupt enum byte just picks a field).
    void selectField(Field field);
    Field selectedField() const;
    // Toggle the edited field (root <-> scale).
    void cycleField();

    // --- built-in scale templates (FR3; the fixed read-only table) ---------
    // How many built-in templates exist (common::scaleTemplateCount()).
    static int scaleCount();
    // The template's display name, verbatim from common::SCALE_TEMPLATES.
    // A total accessor: an out-of-range index clamps to the nearest valid
    // entry so it can never read out of bounds.
    static const char* scaleNameAt(int index);

    // --- per-pattern values (read from the LOCAL UI cache) ------------------
    // The pattern's cached root pitch class (0-11) / template index. An
    // invalid pattern slot reads as 0 (the factory default, C Major).
    int rootValue(uint8_t patternIndex) const;
    int scaleIndexValue(uint8_t patternIndex) const;

    // --- selections (post PATTERN_SCALE_SET commands; FR4) -----------------
    // Select the pattern's root / template index: the local UI cache is
    // updated and ONE PATTERN_SCALE_SET command carrying the pattern's full
    // (root, template) pair is posted to the sequencer core. Out-of-range
    // selections are REFUSED (a root is a pitch class, an index names a real
    // template): false is returned, NOTHING is cached and NOTHING is sent.
    bool setRoot(uint8_t patternIndex, int root);
    bool setScaleIndex(uint8_t patternIndex, int index);
    // Relative selection with WRAP: the root wraps modulo 12, the template
    // index modulo the built-in count. False when the pattern slot is
    // invalid (nothing is cached or sent).
    bool cycleRoot(uint8_t patternIndex, int delta);
    bool cycleScaleIndex(uint8_t patternIndex, int delta);

    // --- IView ---------------------------------------------------------------
    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state,
                               const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    // True while `patternIndex` names one of the fixed pattern slots (the
    // same 16-slot bound the sequencer's pattern state enforces; checked
    // here so refusals are uniform).
    static bool validPattern(uint8_t patternIndex);
    // Post the cached (root, template) pair for one pattern as ONE
    // PATTERN_SCALE_SET command — the view's ONLY cross-core write path.
    void postPatternScale(uint8_t patternIndex);

    hardware::LedMatrix& ledMatrix_;

    // Core0's LOCAL UI cache of the edited values (ui-integration T8): one
    // root + one template byte per pattern slot, seeded with the factory
    // defaults (C Major) and updated by every committed write. This is the
    // view's OWN state — the sequencer core keeps its separate working
    // state and the two communicate only through the command queue.
    // Fixed capacity: one entry per pattern slot.
    uint8_t roots_[common::PATTERN_CAPACITY] = {};
    uint8_t templates_[common::PATTERN_CAPACITY] = {};

    // Which field UP/DOWN cycle (view-local; see the class comment).
    Field field_ = Field::Root;
};

}  // namespace ui
