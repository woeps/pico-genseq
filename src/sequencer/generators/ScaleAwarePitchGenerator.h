#pragma once

#include <cstdint>
#include <type_traits>

#include "../../common/Scale.h"
#include "../GeneratorCapabilities.h"
#include "../IGenerator.h"

// Scale-aware pitch generator plugin (generative-engine-expansion task 5,
// FR2, FR4, AC2).
//
// The pitch generator whose output is ALWAYS constrained to the pattern's
// selected root + scale: every emitted MIDI note is a member of the
// GeneratorContext's common::Scale (FR2's "Scale-aware pitch generator",
// FR4's per-pattern scale selection, AC2). The note is drawn uniformly from
// the in-scale notes inside the configured low/high note range:
//
//   - degree enumeration: the in-scale notes inside [low, high] are
//     enumerated by walking the scale's degrees through
//     Scale::degreeToNote anchored at the octave containing `low` — the
//     shared constraint math's single home, never duplicated here. The
//     walk is a fixed bounded loop (at most kMaxDegreeScan iterations,
//     each an O(12) mask scan inside Scale) over a fixed 128-byte stack
//     table: no heap, no unbounded work (tick-path safe);
//   - the pick: ONE draw from the deterministic, seedable GeneratorRng the
//     context carries (global constraint: all randomness flows through
//     the seeded PRNG, so a fixed seed replays identical output), giving
//     a uniform spread over the whole in-range scale content — every
//     in-scale note in the range is reachable;
//   - range: the low/high note parameters clamp into MIDI 0-127 and an
//     inverted pair swaps (configure() is total). A range at least 12
//     semitones wide always contains an in-scale note for every scale
//     (every scale contains its root); a degenerate range holding no
//     in-scale note falls back to Scale::nearestNoteInScale(low) — the
//     scale constraint outranks the range, and the fallback stays
//     deterministic and in scale;
//   - a null context scale (a pattern carrying no constraint) falls back
//     to the default-constructed Scale (C Major) — the same value
//     PatternGenerativeState defaults to — so the generator is total and
//     never out of scale.
//
//   - capability: PITCH only (the UI shows just the pitch controls, AC8);
//   - parameters: lowNote (id 0) and highNote (id 1), both MIDI note
//     numbers 0..127 with Note hints; factory defaults span two octaves
//     around middle C (48..72).
//
// Global constraints honored here: no heap allocation, no RTTI, no
// exceptions on the generation path; fixed-size members with a
// static_assert bound below. Like the Euclidean plugin, generate() is a
// pure function of (stepIndex, context, configuration) — the only
// randomness is the context's caller-owned, caller-seeded RNG, so
// generation is deterministic and replayable. SDK-free: <cstdint> plus the
// SDK-free Scale and generator interface headers only.

namespace sequencer {

// The scale-aware pitch generator plugin. Default construction yields the
// factory configuration (48..72); the registered instance in
// RegisterGenerators.cpp uses exactly that.
class ScaleAwarePitchGenerator final : public IGenerator {
public:
    // Stable parameter ids (PatternGenerativeState stores configured
    // values indexed by these).
    static constexpr uint8_t kParamLowNote = 0;
    static constexpr uint8_t kParamHighNote = 1;

    // The MIDI note bounds (the range parameters' declared range).
    static constexpr uint8_t kMinNote = 0;
    static constexpr uint8_t kMaxNote = 127;
    // Factory defaults: the two-octave span 48..72 around middle C.
    static constexpr uint8_t kDefaultLowNote = 48;
    static constexpr uint8_t kDefaultHighNote = 72;

    // Fixed bounds of the bounded generation loop (global constraint: no
    // unbounded work). The enumerated in-range notes occupy at most one
    // slot per MIDI note (kMaxRangeNotes = 128), and the degree walk is
    // capped at kMaxDegreeScan iterations — the walk actually terminates
    // earlier, as soon as degreeToNote rises past `high` (the notes
    // strictly increase with the degree), so the cap is a defensive total
    // bound only.
    static constexpr int kMaxRangeNotes = 128;
    static constexpr int kMaxDegreeScan = 256;

    // Factory configuration (48..72).
    ScaleAwarePitchGenerator() = default;
    ScaleAwarePitchGenerator(uint8_t lowNote, uint8_t highNote);

    // Set the note range. Total: both values clamp into MIDI 0-127 and an
    // inverted pair (low > high) swaps, so the stored range is always a
    // valid [low, high] inside 0-127 — callers passing raw UI values need
    // no pre-normalization.
    void configure(uint8_t lowNote, uint8_t highNote);

    // The stored (normalized) note range.
    uint8_t lowNote() const { return lowNote_; }
    uint8_t highNote() const { return highNote_; }

    // IGenerator: stable identity for dispatch, persistence and the UI.
    const char* id() const override { return "builtin.pitch"; }
    const char* name() const override { return "Scale-Aware Pitch"; }
    // PITCH only (FR1, AC8).
    GeneratorCapabilities capabilities() const override {
        return GeneratorCapability::PITCH;
    }
    const GeneratorParamSet& paramDefs() const override;
    // One step's decision: hasPitch is always true (the generator always
    // owns the pitch), pitch is an in-scale note drawn from the configured
    // range with one draw of the context's seeded PRNG. The RNG state is
    // the CALLER's (the sequencer reseeds per pattern/role/step), so
    // identical inputs replay identical decisions.
    void generate(uint32_t stepIndex, const GeneratorContext& context,
                  GeneratorOutput& output) const override;

private:
    // The generator's configuration: the normalized note range.
    uint8_t lowNote_ = kDefaultLowNote;
    uint8_t highNote_ = kDefaultHighNote;
};

inline ScaleAwarePitchGenerator::ScaleAwarePitchGenerator(uint8_t lowNote,
                                                         uint8_t highNote) {
    configure(lowNote, highNote);
}

inline void ScaleAwarePitchGenerator::configure(uint8_t lowNote, uint8_t highNote) {
    // Clamp into MIDI 0-127 (a uint8 can still carry a raw 128-255 value),
    // then swap an inverted pair so the stored range is always valid.
    uint8_t low = (lowNote > kMaxNote) ? kMaxNote : lowNote;
    uint8_t high = (highNote > kMaxNote) ? kMaxNote : highNote;
    if (low > high) {
        const uint8_t swap = low;
        low = high;
        high = swap;
    }
    lowNote_ = low;
    highNote_ = high;
}

inline void ScaleAwarePitchGenerator::generate(uint32_t /*stepIndex*/,
                                               const GeneratorContext& context,
                                               GeneratorOutput& output) const {
    // Reset the output so stale caller fields never leak (the same
    // convention as the other plugins), then emit only the pitch decision.
    output = GeneratorOutput{};

    // FR4/AC2: the scale constraint. A null context scale (a pattern
    // carrying none) falls back to the default Scale — the same C-Major
    // value PatternGenerativeState defaults to — so the generator is
    // total and never out of scale.
    const common::Scale defaultScale;
    const common::Scale& scale =
        (context.scale != nullptr) ? *context.scale : defaultScale;

    // Enumerate every in-scale note inside [low, high]: walk the scale's
    // degrees through the SHARED degreeToNote math, anchored at the octave
    // containing `low` (no in-scale note of the range can lie in a lower
    // octave: the highest note of octave low/12 - 1 is below `low`). The
    // notes strictly increase with the degree, so the walk stops as soon
    // as it rises past `high`; kMaxDegreeScan is the defensive total cap.
    uint8_t inRange[kMaxRangeNotes];
    int count = 0;
    const int baseOctave = static_cast<int>(lowNote_) / 12;
    for (int degree = 0; degree < kMaxDegreeScan; ++degree) {
        const int note = scale.degreeToNote(degree, baseOctave);
        if (note > static_cast<int>(highNote_)) break;
        if (note >= static_cast<int>(lowNote_)) {
            inRange[count] = static_cast<uint8_t>(note);
            ++count;
        }
    }

    output.hasPitch = true;
    if (count == 0) {
        // Degenerate range holding no in-scale note (e.g. a single note
        // outside the scale): the SCALE constraint outranks the range —
        // emit the in-scale note nearest the range's bottom (the shared
        // nearestNoteInScale tie-breaks deterministically to the lower
        // note). Total and deterministic.
        output.pitch = scale.nearestNoteInScale(lowNote_);
        return;
    }

    // One uniform draw over the in-range scale content: every in-scale
    // note of the range is reachable, and a fixed seed replays the same
    // pick. count <= 128 fits the uint8 bound of nextBelow exactly.
    const uint8_t pick = context.rng.nextBelow(static_cast<uint8_t>(count));
    output.pitch = inRange[pick];
}

// The fixed parameter table, declared at namespace scope so the constexpr
// evaluation happens after the class type is complete (the same pattern as
// the sibling plugins).
constexpr GeneratorParamSet makeScalePitchParamDefs() {
    GeneratorParamSet set;
    set.defs[0] = {ScaleAwarePitchGenerator::kParamLowNote, "Low Note",
                   ScaleAwarePitchGenerator::kMinNote, ScaleAwarePitchGenerator::kMaxNote,
                   ScaleAwarePitchGenerator::kDefaultLowNote, GeneratorParamHint::Note};
    set.defs[1] = {ScaleAwarePitchGenerator::kParamHighNote, "High Note",
                   ScaleAwarePitchGenerator::kMinNote, ScaleAwarePitchGenerator::kMaxNote,
                   ScaleAwarePitchGenerator::kDefaultHighNote, GeneratorParamHint::Note};
    set.count = 2;
    return set;
}

constexpr GeneratorParamSet kScalePitchParamDefs = makeScalePitchParamDefs();

inline const GeneratorParamSet& ScaleAwarePitchGenerator::paramDefs() const {
    return kScalePitchParamDefs;
}

// Size bounds on the new plugin state (global constraint): one vtable
// pointer plus the two configuration bytes, a small fixed footprint.
static_assert(sizeof(ScaleAwarePitchGenerator) <= 16,
              "ScaleAwarePitchGenerator stays a small fixed object");

}  // namespace sequencer
