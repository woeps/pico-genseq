// Renders the LED-matrix screens used in docs/user/user-manual.md by driving the
// real view classes on the host. Output: SVG files in the directory given as argv[1].
#include "capture.h"
#include "ui/state/UIState.h"
#include "ui/state/SetLibrary.h"
#include "ui/views/PatternsView.h"
#include "ui/views/InstrumentsView.h"
#include "ui/state/Instruments.h"
#include "ui/state/LoadIndicator.h"
#include "ui/views/GateSetView.h"
#include "ui/views/PitchSetView.h"
#include "ui/views/VelocitySetView.h"
#include "ui/views/SettingsView.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ui;
using state::UIState;

struct Frame {
    std::string caption;
    uint32_t px[256];
};

static hardware::Led led{0};
static hardware::LedMatrix matrix{0};
static PatternsView patterns{matrix};
static InstrumentsView instruments{matrix};
static GateSetView gate{matrix};
static PitchSetView pitch{matrix};
static VelocitySetView velocity{matrix};
static SettingsView settings{led, matrix};

static Frame grab(const std::string& cap, IView& v, const UIState& s) {
    v.render(s);
    Frame f{cap, {}};
    const uint32_t* p = hardware::capturePixels(matrix);
    for (int i = 0; i < 256; i++) f.px[i] = p[i];
    return f;
}
static Frame banner(const std::string& cap, uint32_t color) {
    matrix.fill(color);
    Frame f{cap, {}};
    const uint32_t* p = hardware::capturePixels(matrix);
    for (int i = 0; i < 256; i++) f.px[i] = p[i];
    return f;
}

// Base state: 3 patterns. Pattern 1 and 2 share gate set 0; pattern 3 has gate set 1.
// All three share note set 0 and velocity set 0 unless a scenario changes it.
static UIState base() {
    UIState s;
    s.patternCount = 3;
    s.activePatterns = 0x7;
    s.gateLib.occupied = 0x0003;
    s.noteLib.occupied = 0x0001;
    s.velocityLib.occupied = 0x0001;
    s.patternRefs[2].gate = 1;
    s.gateLib.slots[0].steps = 16; s.gateLib.slots[0].pulses = 5;
    s.instruments[0].members = 0x7;  // one Instrument plays all three
    return s;
}
static void gateEdit(UIState& s, state::GateSetProperty p) {
    s.gateSetDraft = s.gateLib.slots[s.selectedGateSlot];
    s.selectedGateSetProperty = p;
}
static void pitchEdit(UIState& s, uint8_t field) {
    s.pitchSetDraft = s.noteLib.slots[s.selectedNoteSlot];
    s.selectedPitchSetField = field;
}
static void velEdit(UIState& s, uint8_t field) {
    s.velocitySetDraft = s.velocityLib.slots[s.selectedVelocitySlot];
    s.selectedVelocitySetField = field;
}

static uint8_t chan(uint32_t c, int shift) {
    double v = (c >> shift) & 0xFF;
    return (uint8_t)std::lround(255.0 * std::pow(v / 255.0, 0.6));  // lift dim LEDs so they stay visible
}

static void writeSvg(const std::string& dir, const std::string& name, const std::vector<Frame>& frames,
                     const std::vector<std::pair<std::pair<int, int>, std::string>>& notes = {}) {
    const int cell = 14, pad = 12, fw = 16 * cell, gap = 22, capH = 34;
    const int noteW = notes.empty() ? 0 : 190;
    const int n = (int)frames.size();
    const int w = pad * 2 + n * fw + (n - 1) * gap + noteW;
    const int h = pad * 2 + fw + capH;
    FILE* f = fopen((dir + "/" + name + ".svg").c_str(), "w");
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\" font-family=\"sans-serif\" font-size=\"12\">\n", w, h, w, h);
    fprintf(f, "<rect width=\"%d\" height=\"%d\" rx=\"8\" fill=\"#f4f4f5\"/>\n", w, h);
    for (int i = 0; i < n; i++) {
        const int ox = pad + i * (fw + gap), oy = pad;
        fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" rx=\"4\" fill=\"#0b0b0d\"/>\n", ox, oy, fw, fw);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                uint32_t c = frames[i].px[y * 16 + x];
                uint8_t g = chan(c, 16), r = chan(c, 8), b = chan(c, 0);
                if (!(c & 0xFFFFFF))
                    fprintf(f, "<circle cx=\"%d\" cy=\"%d\" r=\"%.1f\" fill=\"#1c1c20\"/>\n", ox + x * cell + cell / 2, oy + y * cell + cell / 2, cell * 0.3);
                else
                    fprintf(f, "<circle cx=\"%d\" cy=\"%d\" r=\"%.1f\" fill=\"#%02x%02x%02x\"/>\n", ox + x * cell + cell / 2, oy + y * cell + cell / 2, cell * 0.4, r, g, b);
            }
        fprintf(f, "<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" fill=\"#222\">%s</text>\n", ox + fw / 2, oy + fw + 20, frames[i].caption.c_str());
    }
    for (auto& nt : notes) {
        const int y0 = pad + nt.first.first * cell, y1 = pad + (nt.first.second + 1) * cell;
        const int bx = pad + n * fw + (n - 1) * gap + 8;
        fprintf(f, "<path d=\"M%d %d h5 V%d h-5\" fill=\"none\" stroke=\"#555\"/>\n", bx, y0 + 2, y1 - 2);
        fprintf(f, "<text x=\"%d\" y=\"%d\" fill=\"#222\">%s</text>\n", bx + 12, (y0 + y1) / 2 + 4, nt.second.c_str());
    }
    fprintf(f, "</svg>\n");
    fclose(f);
}

int main(int argc, char** argv) {
    const std::string d = argc > 1 ? argv[1] : ".";
    using GP = state::GateSetProperty;
    using PS = state::PatternSet;

    // --- Section 2: layout of an editor, and the status pixels ---
    {
        UIState s = base(); s.currentView = state::ViewId::PITCH_SET;
        s.noteLib.slots[0].scale = common::ScaleId::MAJOR;
        pitchEdit(s, state::PITCH_SET_FIELD_PITCH_BASE); s.pitchSetDirty = true;
        writeSvg(d, "layout", {grab("Pitch editor, field p0", pitch, s)},
                 {{{0, 4}, "label (yellow)"}, {{6, 10}, "value (green)"}, {{14, 14}, "users of this set (red)"}, {{15, 15}, "set map"}});
    }
    {
        std::vector<Frame> fr;
        UIState s = base(); s.currentView = state::ViewId::PITCH_SET;
        pitchEdit(s, state::PITCH_SET_FIELD_PITCH_BASE); s.pitchSetDirty = true;
        fr.push_back(grab("uncommitted change", pitch, s));
        s = base(); s.noteLib.slots[0].scale = common::ScaleId::MAJOR; pitchEdit(s, state::PITCH_SET_FIELD_PITCH_BASE);
        fr.push_back(grab("pitch in scale", pitch, s));
        s.noteLib.slots[0].pitches[0] = 61; pitchEdit(s, state::PITCH_SET_FIELD_PITCH_BASE);
        fr.push_back(grab("pitch out of scale", pitch, s));
        s = base(); s.gateLib.slots[0].gateStates[0] = 1; gateEdit(s, GP::STEP_GRID); s.selectedGateStep = 0;
        fr.push_back(grab("gate step on", gate, s));
        s.selectedGateStep = 1; gateEdit(s, GP::STEP_GRID);
        fr.push_back(grab("gate step off", gate, s));
        writeSvg(d, "status-pixels", fr);
    }
    // --- Set rows: shared set ---
    {
        UIState s = base(); s.gateLib.occupied = 0x0007; s.gateSetDraft = s.gateLib.slots[0];
        s.selectedGateSlot = 0; gateEdit(s, GP::STEPS);
        writeSvg(d, "set-rows", {grab("gate set 0 used by patterns 1 and 2", gate, s)},
                 {{{14, 14}, "users: patterns 1, 2"}, {{15, 15}, "sets 0, 1, 2 exist"}});
    }
    // --- Banners ---
    writeSvg(d, "banners", {banner("save succeeded", 0x00FF0000), banner("save failed", 0x0000FF00), banner("saved setup not loaded", 0x00FFFF00)});

    // --- Section 4: PATTERNS ---
    {
        std::vector<Frame> fr;
        UIState s = base(); s.currentView = state::ViewId::PATTERNS; s.selectedPattern = 1;
        s.gateLib.occupied = 0x0003; s.patternRefs[1].gate = 0;
        s.selectedPatternSet = PS::GATE; fr.push_back(grab("GATE stream selected", patterns, s));
        s.selectedPatternSet = PS::PITCH; fr.push_back(grab("PITCH stream selected", patterns, s));
        s.selectedPatternSet = PS::VELOCITY; fr.push_back(grab("VELOCITY stream selected", patterns, s));
        writeSvg(d, "patterns", fr);
        s = base(); s.selectedPattern = 3; s.selectedPatternSet = PS::GATE;
        writeSvg(d, "patterns-add", {grab("add slot selected", patterns, s)});
    }
    // --- Section 5: notices and picker ---
    {
        std::vector<Frame> fr;
        UIState s = base(); gateEdit(s, GP::STEPS);
        s.setNotice = state::SetNotice::LIBRARY_FULL; fr.push_back(grab("library full", gate, s));
        s.setNotice = state::SetNotice::IN_USE; fr.push_back(grab("set in use", gate, s));
        s.setNotice = state::SetNotice::UNUSED; fr.push_back(grab("no pattern uses it", gate, s));
        s.setNotice = state::SetNotice::NONE;
        s.gotoPicker.active = true; s.gotoPicker.type = state::SetType::GATE; s.gotoPicker.candidates = 0x0003; s.gotoPicker.cursor = 1;
        fr.push_back(grab("go-to picker (pattern 2)", gate, s));
        writeSvg(d, "notices", fr);
    }
    // --- Section 7: GATE ---
    {
        UIState s = base(); s.currentView = state::ViewId::GATE_SET;
        s.gateLib.slots[0].patternProbability = 70;
        struct { GP p; const char* c; } g[] = {{GP::ALGORITHM, "Alg"}, {GP::STEPS, "StP"}, {GP::PULSES, "PLS"}, {GP::ROTATION, "rot"},
                                               {GP::NOTE_LENGTH, "nLn"}, {GP::LENGTH, "LEn"}, {GP::PROBABILITY, "Prb"}};
        std::vector<Frame> a, b;
        int i = 0;
        for (auto& e : g) { gateEdit(s, e.p); (i++ < 4 ? a : b).push_back(grab(e.c, gate, s)); }
        writeSvg(d, "gate-fields-1", a); writeSvg(d, "gate-fields-2", b);
        std::vector<Frame> st;
        s.gateLib.slots[0].gateStates[0] = 1; s.gateLib.slots[0].stepProbability[0] = 100;
        s.gateLib.slots[0].stepProbability[3] = 50; s.gateLib.slots[0].gateStates[3] = 1;
        s.selectedGateStep = 0; gateEdit(s, GP::STEP_GRID); st.push_back(grab("s0: on, 100 %", gate, s));
        s.selectedGateStep = 1; gateEdit(s, GP::STEP_GRID); st.push_back(grab("s1: off, inherit", gate, s));
        s.selectedGateStep = 3; gateEdit(s, GP::STEP_GRID); st.push_back(grab("s3: on, 50 %", gate, s));
        writeSvg(d, "gate-grid", st);
    }
    // --- Section 8: PITCH ---
    {
        UIState s = base(); s.currentView = state::ViewId::PITCH_SET;
        auto& c = s.noteLib.slots[0];
        c.scale = common::ScaleId::MAJOR; c.scaleRoot = 1;  // C#
        c.quantize = common::QuantizeMode::NEAREST;
        std::vector<Frame> a, b;
        pitchEdit(s, state::PITCH_SET_FIELD_SCALE); a.push_back(grab("Scl", pitch, s));
        pitchEdit(s, state::PITCH_SET_FIELD_ROOT); a.push_back(grab("rut (C#)", pitch, s));
        pitchEdit(s, state::PITCH_SET_FIELD_QUANTIZE); a.push_back(grab("qnt", pitch, s));
        pitchEdit(s, state::PITCH_SET_FIELD_COUNT); b.push_back(grab("Cnt", pitch, s));
        pitchEdit(s, state::PITCH_SET_FIELD_ORDER); b.push_back(grab("Ord", pitch, s));
        c.scale = common::ScaleId::CHROMATIC; c.scaleRoot = 0;
        pitchEdit(s, state::PITCH_SET_FIELD_PITCH_BASE); b.push_back(grab("p0 (C4)", pitch, s));
        c.pitches[1] = 66; pitchEdit(s, state::PITCH_SET_FIELD_PITCH_BASE + 1); b.push_back(grab("p1 (F#4)", pitch, s));
        writeSvg(d, "pitch-fields-1", a); writeSvg(d, "pitch-fields-2", b);
    }
    // --- Section 9: VELOCITY ---
    {
        UIState s = base(); s.currentView = state::ViewId::VELOCITY_SET;
        auto& c = s.velocityLib.slots[0];
        c.humanizeRange = 15; c.seedSet = true; c.seed = 7; c.coupling = common::CouplingMode::LOCKED;
        std::vector<Frame> a, b;
        velEdit(s, state::VELOCITY_SET_FIELD_HUMANIZE); a.push_back(grab("Hmn", velocity, s));
        velEdit(s, state::VELOCITY_SET_FIELD_SEED); a.push_back(grab("Sed 7", velocity, s));
        c.seedSet = false; velEdit(s, state::VELOCITY_SET_FIELD_SEED); a.push_back(grab("Sed off", velocity, s));
        velEdit(s, state::VELOCITY_SET_FIELD_COUPLING); a.push_back(grab("Cpl", velocity, s));
        velEdit(s, state::VELOCITY_SET_FIELD_COUNT); b.push_back(grab("Cnt", velocity, s));
        velEdit(s, state::VELOCITY_SET_FIELD_ORDER); b.push_back(grab("Ord", velocity, s));
        velEdit(s, state::VELOCITY_SET_FIELD_VELOCITY_BASE); b.push_back(grab("v0", velocity, s));
        writeSvg(d, "velocity-fields-1", a); writeSvg(d, "velocity-fields-2", b);
    }
    // --- Instruments (section 5a) ---
    {
        // Five patterns. Instrument 1 (channel 1): patterns 1-3 assigned, pattern 3 stopped
        // (inactive), pattern 4 not assigned. Instrument 2 (channel 2): patterns 1 and 5.
        // Pattern 1 is therefore shared between both Instruments.
        UIState s = base();
        s.currentView = state::ViewId::INSTRUMENTS;
        s.patternCount = 5;
        s.activePatterns = 0b11011;
        s.instruments[0].members = 0b00111;
        uint8_t slot = 0;
        state::createInstrumentSlot(s, slot);
        s.instruments[1].members = 0b10001;
        s.selectedInstrument = 0;
        s.instrumentCursor = 3;
        matrix.clear();  // as InstrumentsView::onEnter does on the device
        Frame a = grab("Instrument 1, cursor on pattern 4", instruments, s);
        s.selectedInstrument = 1;
        s.instrumentCursor = 4;
        Frame b = grab("Instrument 2", instruments, s);
        s.instruments[1].muted = true;
        Frame c = grab("Instrument 2 muted", instruments, s);
        s.instruments[1].muted = false;
        s.instrumentNotice = state::InstrumentNotice::FULL;  // e.g. Ctrl+N with all 16 in use
        s.instrumentCursor = 2;
        Frame n = grab("Refused action", instruments, s);
        writeSvg(d, "instruments", {a, b, c, n});
        // The same setup in the PATTERNS view: pattern 1 is in both Instruments, patterns 2-4
        // only in Instrument 1, pattern 5 only in Instrument 2; one more pattern is in neither.
        UIState p = s;
        p.instrumentNotice = state::InstrumentNotice::NONE;
        p.currentView = state::ViewId::PATTERNS;
        p.patternCount = 6;
        p.activePatterns = 0b111111;
        p.instruments[0].members = 0b001111;  // Instrument 1: patterns 1-4
        p.instruments[1].members = 0b010001;  // Instrument 2: patterns 1 and 5; pattern 6 in none
        p.selectedInstrument = 0;
        p.selectedPattern = 1;
        matrix.clear();  // as PatternsView::onEnter does on the device
        Frame pa = grab("Selected Instrument 1", patterns, p);
        p.selectedInstrument = 1;
        Frame pb = grab("Selected Instrument 2", patterns, p);
        writeSvg(d, "patterns-instruments", {pa, pb});
    }
    // --- Load indicator (section 2): UIController draws it after every view render with the
    // same LoadIndicator helpers used here. ---
    {
        UIState s = base(); s.currentView = state::ViewId::PATTERNS; s.selectedPattern = 0;
        matrix.clear();
        patterns.render(s);
        matrix.setPixel(state::kLoadPixelX, state::kLoadPixelY, state::loadColor(state::LoadLevel::AMBER));
        Frame a{"Amber: notes are waiting", {}};
        const uint32_t* p = hardware::capturePixels(matrix);
        for (int i = 0; i < 256; i++) a.px[i] = p[i];
        UIState t = base(); t.currentView = state::ViewId::INSTRUMENTS; t.timingFaultLatched = true;
        matrix.clear();
        instruments.render(t);
        matrix.setPixel(state::kLoadPixelX, state::kLoadPixelY, state::loadColor(state::LoadLevel::RED));
        Frame b{"Red: overload, stopped", {}};
        p = hardware::capturePixels(matrix);
        for (int i = 0; i < 256; i++) b.px[i] = p[i];
        writeSvg(d, "load-pixel", {a, b}, {{{4, 4}, "load pixel (column 16)"}});
    }
    // --- Section 10 ---
    {
        UIState s = base(); s.bpm = 120; s.currentView = state::ViewId::SETTINGS;
        matrix.clear();
        Frame a = grab("SETTINGS (F1)", settings, s);
        writeSvg(d, "settings", {a});
    }
    return 0;
}
