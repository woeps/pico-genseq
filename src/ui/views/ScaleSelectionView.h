#pragma once

#include <cstdint>

#include "IView.h"
#include "../hardware/LedMatrix.h"
#include "../../common/ScaleTemplates.h"
#include "../../sequencer/ParameterAddress.h"
#include "../../sequencer/ParameterBus.h"

namespace ui {

// Per-pattern root note + scale selection view
// (generative-engine-expansion task 19, FR4).
//
// The view selects ONE pattern's scale constraint: a root pitch class
// (0-11, C..B) and an index into the fixed built-in template table
// (common::SCALE_TEMPLATES — the FR3 set; user-definable scales are out of
// scope). The selected pair is what constrains pitch variation for that
// pattern: the scale-aware pitch generator and the pitch mutation operators
// read it through PatternGenerativeState::scale() / the bus.
//
// Selections write through the ParameterBus BASE values (task 16): a
// committed selection is a setBase on ParamId::ScaleRoot / ParamId::ScaleTemplate
// for the selected pattern slot — the stored live sequencer state (the
// per-pattern generative working state) changes; no modulation overlay is
// ever armed (overlays belong to the modulation layer alone). The bus clamps
// every write into the task-16 table range, which matches the view's own
// root/template bounds.
//
// Selection state is view-local by design: UIState carries no scale fields
// (adding them is outside task 19's declared scope), so which FIELD the
// keys adjust (root / scale) lives in this view while the edited VALUES
// live in the single authoritative sequencer state through the bus.
// handleEvent therefore returns the UIState unchanged.
//
// Key mapping (documented, follows the sibling view conventions):
//   LEFT / RIGHT   - toggle the edited field (root <-> scale)
//   UP / DOWN      - cycle the selected field by 1 with WRAP (SHIFT: coarse 5)
//
// Global constraints honored here: no heap allocation, no unbounded loops
// (every operation is O(1) arithmetic or a compile-time-bounded table read),
// fixed-capacity storage (two enum scalars), built-in templates only. The
// view never touches the tick path.
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

    ScaleSelectionView(sequencer::ParameterBus& bus,
                       hardware::LedMatrix& ledMatrix);

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

    // --- per-pattern values (read through the bus base values) -------------
    // The pattern's stored root pitch class (0-11) / template index. An
    // invalid pattern slot reads as 0 (the bus rejects the address).
    int rootValue(uint8_t patternIndex) const;
    int scaleIndexValue(uint8_t patternIndex) const;

    // --- selections (write through the bus base values; FR4) ---------------
    // Select the pattern's root / template index. Out-of-range selections
    // are REFUSED (a root is a pitch class, an index names a real template):
    // false is returned and NOTHING is written.
    bool setRoot(uint8_t patternIndex, int root);
    bool setScaleIndex(uint8_t patternIndex, int index);
    // Relative selection with WRAP: the root wraps modulo 12, the template
    // index modulo the built-in count. False when the pattern slot is
    // invalid or the bus rejects the write.
    bool cycleRoot(uint8_t patternIndex, int delta);
    bool cycleScaleIndex(uint8_t patternIndex, int delta);

    // --- IView ---------------------------------------------------------------
    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state,
                               const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    // True while `patternIndex` names one of the fixed pattern slots (the
    // same bound the bus enforces; checked here so refusals are uniform).
    static bool validPattern(uint8_t patternIndex);
    // One per-pattern bus address for each edited parameter.
    static sequencer::ParameterAddress rootAddress(uint8_t patternIndex);
    static sequencer::ParameterAddress scaleAddress(uint8_t patternIndex);

    sequencer::ParameterBus& bus_;
    hardware::LedMatrix& ledMatrix_;

    // Which field UP/DOWN cycle (view-local; see the class comment).
    Field field_ = Field::Root;
};

}  // namespace ui
