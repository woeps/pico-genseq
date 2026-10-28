#pragma once

#include "IView.h"
#include "../hardware/Led.h"
#include "../hardware/LedMatrix.h"
#include "../../common/ScaleTemplates.h"

namespace ui {

// Global Settings view (ui-integration T14, FR4/FR12-FR14, AC7/AC10).
//
// Two fields share the view: the global BPM and the GLOBAL SCALE — a
// combined field of a root pitch class (0-11, C..B) plus an index into the
// fixed built-in template table (common::SCALE_TEMPLATES). The scale is a
// TRUE global setting like the BPM: its single source of truth lives on the
// sequencer core (TransportTimeline) and every pattern reads it from there.
// This view edits the UIState MIRROR of that global (state.scaleRoot /
// state.scaleTemplateIndex) and every change crosses the cores ONLY as a
// SCALE_SET command dispatched through the Reducer (state::setScale ->
// commands::sendScaleCommand) — the view never touches the ParameterBus and
// shares no data structure with core1 (FR17).
//
// Key mapping (follows the sibling view conventions):
//   ENTER        - cycle the edited field (bPM <-> Scale); held repeats ignored
//   UP / DOWN    - Field::BPM:   BPM +/-1 (SHIFT: +/-10)
//                  Field::Scale: scaleRoot +/-1 with WRAP 0-11 (SHIFT: +/-5)
//   LEFT / RIGHT - Field::Scale: scaleTemplateIndex +/-1 with WRAP across the
//                  built-in templates (SHIFT: +/-5); unbound on Field::BPM
//
// Rendering: the 16x16 matrix holds a 3-char label region plus a note/number
// region. Field::Scale shows the scale NAME (from ScaleTemplates) condensed
// to the 3-char label slot and the ROOT as a note name (C..B, sharps) on the
// note region.
//
// Global constraints honored here: no heap allocation, no unbounded loops
// (every operation is O(1) arithmetic or a compile-time-bounded table read),
// built-in templates only, commands are the only cross-core path.
class SettingsView : public IView {
public:
    // Which of the view's fields UP/DOWN adjust.
    enum class Field : uint8_t {
        BPM = 0,
        SCALE = 1,
    };

    // Root pitch classes: C through B.
    static constexpr int kRootCount = 12;
    // The coarse key step (SHIFT+UP/DOWN/LEFT/RIGHT), sibling-view convention.
    static constexpr int kCoarseStep = 5;

    SettingsView(hardware::Led& led, hardware::LedMatrix& ledMatrix);

    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state, const events::Event& event) override;
    void render(const state::UIState& state) override;

    // --- field selection ---------------------------------------------------
    // Select which field UP/DOWN adjust. Total: a corrupt enum byte just
    // picks one of the two fields.
    void selectField(Field field);
    Field selectedField() const;
    // Toggle the edited field (bPM <-> Scale).
    void cycleField();

    // --- scale rendering helpers ---------------------------------------------
    // The root's display name (C..B, sharps) in the 4-byte slot the matrix
    // note driver expects. Total: out-of-range roots clamp to the nearest
    // valid pitch class.
    static void rootNoteName(int root, char (&out)[4]);
    // The template's display NAME, verbatim from common::SCALE_TEMPLATES.
    // Total: an out-of-range index clamps to the nearest valid entry so the
    // accessor can never read out of bounds.
    static const char* scaleNameAt(int index);
    // The template's name condensed to the 3-char matrix label slot: the
    // first three characters of the verbatim ScaleTemplates name (e.g.
    // "Major" -> "Maj", "Natural Minor" -> "Nat"). The label region holds
    // exactly three characters, so longer names are TRUNCATED, never
    // invented; a name shorter than three characters is padded with blanks.
    static void scaleNameLabel(int index, char (&out)[4]);
    // How many built-in templates exist (common::scaleTemplateCount()).
    static int scaleTemplateCount();

private:
    hardware::Led& led;
    hardware::LedMatrix& ledMatrix;
    Field field = Field::BPM;
};

} // namespace ui
