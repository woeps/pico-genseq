// generative-engine-expansion task 1 — built-in scale templates + scale math
// (FR3, FR4, AC2).
//
// Validates the fixed, read-only table of built-in scale templates (FR3) and
// the Scale value type that pitch generation and pitch mutation constrain
// their output with (FR4, AC2):
//
//   1. the template table has between 20 and 30 entries;
//   2. every template has a unique non-empty name, a non-empty interval set
//      within 0-11 semitones, and includes 0 (the root);
//   3. Scale::containsNote is checked against the template mask for every
//      root 0-11 across the full MIDI range 0-127;
//   4. nearestNoteInScale always returns an in-scale note within 0-127 and
//      breaks distance ties deterministically (the LOWER note wins);
//   5. degreeToNote / noteToDegree round-trip (per-octave degree index),
//      including octave wrapping for degrees outside [0, degreeCount);
//   6. Chromatic contains every note.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/ so
// the property-test glob (test/*.cpp) never sweeps it; registered in the host
// test build under the ctest name "test_scale".

#include "Scale.h"
#include "ScaleTemplates.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using common::Scale;
using common::ScaleTemplate;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

constexpr int MIDI_MIN = 0;
constexpr int MIDI_MAX = 127;

// Circular pitch-class distance of `note` above `root` (both plain ints).
int pitchClassOf(int note, int root) {
    return ((note - root) % 12 + 12) % 12;
}

bool maskHasInterval(int templateIndex, int interval) {
    return (common::scaleTemplate(templateIndex).intervals >> interval) & 1u;
}

int indexOfName(const char* name) {
    const int count = common::scaleTemplateCount();
    for (int i = 0; i < count; ++i)
        if (std::strcmp(common::scaleTemplate(i).name, name) == 0) return i;
    return -1;
}

// --- 1 + 2: the fixed read-only template table ------------------------------

void test_template_table() {
    const int count = common::scaleTemplateCount();
    check(count >= 20 && count <= 30, "template count is between 20 and 30");
    for (int i = 0; i < count; ++i) {
        const ScaleTemplate& t = common::scaleTemplate(i);
        check(t.name != nullptr && t.name[0] != '\0', "template name non-empty");
        check(t.intervals != 0, "template interval set non-empty");
        check((t.intervals & 1u) != 0, "template includes 0 as the root");
        check((t.intervals & ~0x0FFFu) == 0, "template intervals stay inside 0-11");
        for (int j = i + 1; j < count; ++j)
            check(std::strcmp(t.name, common::scaleTemplate(j).name) != 0,
                  "template names are unique");
    }
}

// The groups FR3 requires: common scales, the church modes, exotic scales
// (Arabic, Hungarian, Japanese) and chromatic/cluster sets.
void test_required_groups_present() {
    const char* REQUIRED[] = {
        "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor",
        "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian",
        "Locrian",
        "Major Pentatonic", "Minor Pentatonic", "Blues",
        "Whole Tone",
        "Diminished (Whole-Half)", "Diminished (Half-Whole)",
        "Hungarian Minor", "Hungarian Major",
        "Double Harmonic (Arabic)", "Phrygian Dominant",
        "Hirajoshi", "In-Sen", "Iwato", "Kumoi",
        "Chromatic",
        "Semitone Cluster", "Tone Cluster",
    };
    for (const char* name : REQUIRED)
        check(indexOfName(name) >= 0, "required template group present");
}

// The accessor is total: an out-of-range index clamps (never reads OOB).
void test_template_accessor() {
    const int count = common::scaleTemplateCount();
    check(std::strcmp(common::scaleTemplate(0).name, common::scaleTemplate(0).name) == 0,
          "scaleTemplate(0) is stable");
    check(std::strcmp(common::scaleTemplate(-1).name, common::scaleTemplate(0).name) == 0,
          "negative index clamps to the first template");
    check(std::strcmp(common::scaleTemplate(count).name, common::scaleTemplate(count - 1).name) == 0,
          "index == count clamps to the last template");
    check(std::strcmp(common::scaleTemplate(12345).name, common::scaleTemplate(count - 1).name) == 0,
          "large index clamps to the last template");
}

// --- known musical shapes (root 0), verified against theory ----------------

void checkShape(const char* name, const int* pcs, int pcCount) {
    const int idx = indexOfName(name);
    check(idx >= 0, "shape template exists");
    if (idx < 0) return;
    const Scale s(0, static_cast<uint8_t>(idx));
    check(s.degreeCount() == pcCount, "shape degree count matches interval count");
    for (int pc = 0; pc < 12; ++pc) {
        bool expected = false;
        for (int k = 0; k < pcCount; ++k) expected = expected || (pcs[k] == pc);
        check(s.containsNote(static_cast<uint8_t>(pc)) == expected,
              "shape pitch-class membership matches theory");
    }
}

void test_known_shapes() {
    const int kMajor[]      = {0, 2, 4, 5, 7, 9, 11};  checkShape("Major", kMajor, 7);
    const int kNatMinor[]   = {0, 2, 3, 5, 7, 8, 10};  checkShape("Natural Minor", kNatMinor, 7);
    const int kHarmMinor[]  = {0, 2, 3, 5, 7, 8, 11};  checkShape("Harmonic Minor", kHarmMinor, 7);
    const int kMelMinor[]   = {0, 2, 3, 5, 7, 9, 11};  checkShape("Melodic Minor", kMelMinor, 7);
    const int kIonian[]     = {0, 2, 4, 5, 7, 9, 11};  checkShape("Ionian", kIonian, 7);
    const int kDorian[]     = {0, 2, 3, 5, 7, 9, 10};  checkShape("Dorian", kDorian, 7);
    const int kPhrygian[]   = {0, 1, 3, 5, 7, 8, 10};  checkShape("Phrygian", kPhrygian, 7);
    const int kLydian[]     = {0, 2, 4, 6, 7, 9, 11};  checkShape("Lydian", kLydian, 7);
    const int kMixolydian[]  = {0, 2, 4, 5, 7, 9, 10};  checkShape("Mixolydian", kMixolydian, 7);
    const int kAeolian[]    = {0, 2, 3, 5, 7, 8, 10};  checkShape("Aeolian", kAeolian, 7);
    const int kLocrian[]    = {0, 1, 3, 5, 6, 8, 10};  checkShape("Locrian", kLocrian, 7);
    const int kMajPent[]    = {0, 2, 4, 7, 9};         checkShape("Major Pentatonic", kMajPent, 5);
    const int kMinPent[]    = {0, 3, 5, 7, 10};        checkShape("Minor Pentatonic", kMinPent, 5);
    const int kBlues[]      = {0, 3, 5, 6, 7, 10};     checkShape("Blues", kBlues, 6);
    const int kWholeTone[]  = {0, 2, 4, 6, 8, 10};     checkShape("Whole Tone", kWholeTone, 6);
    const int kDimWH[]      = {0, 2, 3, 5, 6, 8, 9, 11};  checkShape("Diminished (Whole-Half)", kDimWH, 8);
    const int kDimHW[]      = {0, 1, 3, 4, 6, 7, 9, 10};  checkShape("Diminished (Half-Whole)", kDimHW, 8);
    const int kHunMinor[]   = {0, 2, 3, 6, 7, 8, 11};  checkShape("Hungarian Minor", kHunMinor, 7);
    const int kHunMajor[]   = {0, 3, 4, 6, 7, 9, 10};  checkShape("Hungarian Major", kHunMajor, 7);
    const int kDoubleHarm[] = {0, 1, 4, 5, 7, 8, 11};  checkShape("Double Harmonic (Arabic)", kDoubleHarm, 7);
    const int kPhryDom[]    = {0, 1, 4, 5, 7, 8, 10};  checkShape("Phrygian Dominant", kPhryDom, 7);
    const int kHirajoshi[]  = {0, 2, 3, 7, 8};         checkShape("Hirajoshi", kHirajoshi, 5);
    const int kInSen[]      = {0, 1, 5, 7, 10};        checkShape("In-Sen", kInSen, 5);
    const int kIwato[]      = {0, 1, 5, 6, 10};        checkShape("Iwato", kIwato, 5);
    const int kKumoi[]      = {0, 2, 3, 7, 9};         checkShape("Kumoi", kKumoi, 5);
    const int kCluster[]    = {0, 1, 2, 3};            checkShape("Semitone Cluster", kCluster, 4);
    const int kToneClust[]  = {0, 2, 4, 6};            checkShape("Tone Cluster", kToneClust, 4);
}

// --- 3: containsNote across every root and the full MIDI range --------------

void test_contains_note_all_roots() {
    const int count = common::scaleTemplateCount();
    for (int t = 0; t < count; ++t) {
        for (int root = 0; root < 12; ++root) {
            const Scale s(static_cast<uint8_t>(root), static_cast<uint8_t>(t));
            int perOctave = 0;
            for (int note = MIDI_MIN; note <= MIDI_MAX; ++note) {
                const bool expected = maskHasInterval(t, pitchClassOf(note, root));
                check(s.containsNote(static_cast<uint8_t>(note)) == expected,
                      "containsNote matches the template mask for every root and note");
                if (note >= 12 && note < 24) perOctave += expected ? 1 : 0;
            }
            check(perOctave == s.degreeCount(),
                  "one full octave holds exactly degreeCount in-scale notes");
        }
    }
}

// --- 4: nearestNoteInScale is total, in-scale and lower-preferring -----------

void test_nearest_note() {
    const int count = common::scaleTemplateCount();
    for (int t = 0; t < count; ++t) {
        for (int root = 0; root < 12; ++root) {
            const Scale s(static_cast<uint8_t>(root), static_cast<uint8_t>(t));
            bool inScale[MIDI_MAX + 1];
            bool any = false;
            for (int n = MIDI_MIN; n <= MIDI_MAX; ++n) {
                inScale[n] = s.containsNote(static_cast<uint8_t>(n));
                any = any || inScale[n];
            }
            check(any, "every scale has at least one in-scale note in 0-127");
            for (int note = MIDI_MIN; note <= MIDI_MAX; ++note) {
                const int r = s.nearestNoteInScale(static_cast<uint8_t>(note));
                check(r >= MIDI_MIN && r <= MIDI_MAX, "nearest stays within MIDI 0-127");
                check(inScale[r], "nearest result is in scale");
                if (inScale[note]) {
                    check(r == note, "an in-scale note maps to itself");
                    continue;
                }
                // Independent reference: ascending scan keeping strictly smaller
                // distances, so an equal-distance tie keeps the earlier (lower)
                // candidate.
                int best = -1;
                int bestDist = 0;
                for (int cand = MIDI_MIN; cand <= MIDI_MAX; ++cand) {
                    if (!inScale[cand]) continue;
                    const int dist = cand > note ? cand - note : note - cand;
                    if (best < 0 || dist < bestDist) {
                        best = cand;
                        bestDist = dist;
                    }
                }
                check(r == best, "nearest matches the lower-preferring reference");
            }
        }
    }
}

// Explicit deterministic tie-break and range-edge cases.
void test_nearest_tie_break() {
    const int majorIdx = indexOfName("Major");
    check(majorIdx >= 0, "Major template exists");
    if (majorIdx >= 0) {
        const Scale major0(0, static_cast<uint8_t>(majorIdx));
        check(major0.nearestNoteInScale(1) == 0, "equidistant 0/2 -> lower 0");
        check(major0.nearestNoteInScale(6) == 5, "equidistant 5/7 -> lower 5");
        check(major0.nearestNoteInScale(11) == 11, "leading tone is in scale");
        const Scale major11(11, static_cast<uint8_t>(majorIdx));
        check(major11.nearestNoteInScale(0) == 1,
              "0 out of a root-11 Major scale -> first in-scale note above");
    }
    const int pentIdx = indexOfName("Minor Pentatonic");
    check(pentIdx >= 0, "Minor Pentatonic template exists");
    if (pentIdx >= 0) {
        const Scale pent(0, static_cast<uint8_t>(pentIdx));
        check(pent.nearestNoteInScale(1) == 0, "distance 1 to 0 beats distance 2 to 3");
        check(pent.nearestNoteInScale(2) == 3, "distance 1 to 3 beats distance 2 to 0");
        check(pent.nearestNoteInScale(4) == 3, "equidistant 3/5 -> lower 3");
        check(pent.nearestNoteInScale(6) == 5, "equidistant 5/7 -> lower 5");
        check(pent.nearestNoteInScale(11) == 10, "equidistant 10/12 -> lower 10");
    }
    const int clusterIdx = indexOfName("Semitone Cluster");
    check(clusterIdx >= 0, "Semitone Cluster template exists");
    if (clusterIdx >= 0) {
        const Scale cluster(5, static_cast<uint8_t>(clusterIdx));
        check(cluster.nearestNoteInScale(0) == 5, "no in-scale note below -> nearest above");
        check(cluster.nearestNoteInScale(3) == 5, "clamps up across a wide gap");
        check(cluster.nearestNoteInScale(127) == 127, "top-of-range in-scale note maps to itself");
        const Scale cluster11(11, static_cast<uint8_t>(clusterIdx));
        check(cluster11.nearestNoteInScale(127) == 122,
              "top-of-range out-of-scale note snaps down into range");
    }
}

// --- 5: degreeToNote / noteToDegree round-trip -------------------------------

void test_degree_roundtrip() {
    const int count = common::scaleTemplateCount();
    for (int t = 0; t < count; ++t) {
        for (int root = 0; root < 12; ++root) {
            const Scale s(static_cast<uint8_t>(root), static_cast<uint8_t>(t));
            const int n = s.degreeCount();
            check(n >= 1 && n <= 12, "degreeCount is between 1 and 12");

            // Forward: every degree of every octave is an in-scale note that
            // maps back to the same per-octave degree index.
            for (int octave = 0; octave <= 10; ++octave) {
                int prev = -1;
                for (int d = 0; d < n; ++d) {
                    const int note = s.degreeToNote(d, octave);
                    check(prev < 0 || note > prev, "degrees ascend within an octave");
                    prev = note;
                    if (note >= MIDI_MIN && note <= MIDI_MAX) {
                        check(s.containsNote(static_cast<uint8_t>(note)),
                              "degreeToNote yields an in-scale note");
                        check(s.noteToDegree(static_cast<uint8_t>(note)) == d,
                              "noteToDegree(degreeToNote(d)) == d");
                    }
                }
                // Octave wrapping in both directions.
                check(s.degreeToNote(n, octave) == s.degreeToNote(0, octave + 1),
                      "degree n wraps to the next octave's root");
                check(s.degreeToNote(-1, octave) == s.degreeToNote(n - 1, octave - 1),
                      "degree -1 wraps to the previous octave's last degree");
                if (octave >= 1)
                    check(s.degreeToNote(-n, octave) == s.degreeToNote(0, octave - 1),
                          "degree -n wraps to the previous octave's root");
            }

            // Reverse: every in-scale note decodes to a degree that re-encodes
            // to the same note at some octave; out-of-scale notes decode to -1.
            for (int note = MIDI_MIN; note <= MIDI_MAX; ++note) {
                if (!s.containsNote(static_cast<uint8_t>(note))) {
                    check(s.noteToDegree(static_cast<uint8_t>(note)) == -1,
                          "out-of-scale note decodes to degree -1");
                    continue;
                }
                const int d = s.noteToDegree(static_cast<uint8_t>(note));
                check(d >= 0 && d < n, "in-scale note decodes to a valid degree index");
                // Every in-scale note is degree d of some root anchor
                // 12*octave + root; that anchor can sit one octave below
                // MIDI 0 (e.g. note 0 relative to a root-11 scale), so the
                // search starts at octave -1.
                bool reached = false;
                for (int octave = -1; octave <= 10 && !reached; ++octave)
                    reached = (s.degreeToNote(d, octave) == note);
                check(reached, "degreeToNote(noteToDegree(note)) == note at some octave");
            }
        }
    }
}

// --- 6: Chromatic contains every note ---------------------------------------

void test_chromatic() {
    const int idx = indexOfName("Chromatic");
    check(idx >= 0, "Chromatic template exists");
    if (idx < 0) return;
    for (int root = 0; root < 12; ++root) {
        const Scale s(static_cast<uint8_t>(root), static_cast<uint8_t>(idx));
        check(s.degreeCount() == 12, "chromatic has 12 degrees");
        for (int note = MIDI_MIN; note <= MIDI_MAX; ++note) {
            check(s.containsNote(static_cast<uint8_t>(note)),
                  "chromatic contains every note");
            check(s.noteToDegree(static_cast<uint8_t>(note)) == pitchClassOf(note, root),
                  "chromatic degree equals the pitch-class offset");
            check(s.nearestNoteInScale(static_cast<uint8_t>(note)) == note,
                  "chromatic nearest is the identity");
        }
    }
}

// --- value-type hygiene ------------------------------------------------------

void test_default_and_clamping() {
    const int count = common::scaleTemplateCount();
    const Scale def;
    check(def.root() == 0 && def.templateIndex() == 0, "default scale is Major rooted at C");
    check(def.containsNote(0) && def.degreeCount() == 7, "default scale is usable");

    const Scale wrapped(13, 255);
    check(wrapped.root() == 1, "root wraps into 0-11");
    check(wrapped.templateIndex() == count - 1, "template index clamps to the table");

    const Scale clamped(5, static_cast<uint8_t>(count));
    check(clamped.root() == 5, "valid root preserved");
    check(clamped.templateIndex() == count - 1, "index == count clamps to the last template");
}

}  // namespace

int main() {
    test_template_table();
    test_required_groups_present();
    test_template_accessor();
    test_known_shapes();
    test_contains_note_all_roots();
    test_nearest_note();
    test_nearest_tie_break();
    test_degree_roundtrip();
    test_chromatic();
    test_default_and_clamping();

    if (failures == 0) {
        std::printf("test_scale: all checks passed\n");
        return 0;
    }
    std::printf("test_scale: %d check(s) failed\n", failures);
    return 1;
}
