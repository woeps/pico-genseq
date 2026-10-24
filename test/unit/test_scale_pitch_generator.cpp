// generative-engine-expansion task 5 — scale-aware pitch generator plugin
// (FR2, FR4, AC2).
//
// Validates the ScaleAwarePitchGenerator plugin: a pitch generator whose
// output is ALWAYS constrained to the pattern's selected root + scale
// (FR2's "Scale-aware pitch generator", FR4's per-pattern scale selection,
// AC2's "scale constraints limit pitch mutation/generation to the selected
// scale intervals"), drawn uniformly over the in-scale notes inside the
// configured low/high note range using the deterministic seeded PRNG the
// GeneratorContext carries (global constraint: randomness uses
// deterministic, seedable PRNGs).
//
// Validates:
//   1. for EVERY built-in scale template and EVERY root 0-11, every
//      generated pitch satisfies Scale::containsNote — across the factory
//      range, the full MIDI range, a shifted mid range and a degenerate
//      single-note range that forces the total out-of-range fallback
//      (which must still stay in scale);
//   2. determinism: a fixed seed replays the identical pitch sequence, a
//      fresh per-step rng with the same seed reproduces the same decision
//      (the sequencer's per-(pattern, role, step) reseeding), and a
//      different seed eventually produces a different pitch;
//   3. the low/high range parameter is respected within MIDI 0-127 for
//      every scale and root (ranges at least 12 semitones wide always
//      contain an in-scale note for every scale, so the range must hold
//      exactly), including after normalization of out-of-range configure()
//      input (clamping to 0-127, low>high swap);
//   4. capabilities equal PITCH only (AC8: the UI shows just the pitch
//      controls), and generate() clears every field it does not produce;
//   5. every in-scale note inside the range is reachable across seeds
//      (the degree pick is spread over the whole in-range scale content,
//      never stuck on one note);
//   6. a null context scale (a pattern carrying no constraint) falls back
//      to the default C-Major constraint — total, never out of scale;
//   7. paramDefs expose the low/high note parameters with the Note hint;
//   8. registration: registerGenerators() (RegisterGenerators.cpp, the
//      ONLY built-in listing, AC1) registers the plugin under
//      "builtin.pitch" with the factory configuration, alongside the
//      Euclidean gate generator; repeated registration adds nothing; the
//      id fits PatternGenerativeState's fixed generator-id storage.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_scale_pitch_generator".

#include "GeneratorCapabilities.h"
#include "GeneratorRegistry.h"
#include "IGenerator.h"
#include "sequencer/PatternGenerativeState.h"
#include "sequencer/generators/ScaleAwarePitchGenerator.h"

#include "common/Scale.h"
#include "common/ScaleTemplates.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

using sequencer::GeneratorCapability;
using sequencer::GeneratorCapabilities;
using sequencer::GeneratorContext;
using sequencer::GeneratorOutput;
using sequencer::GeneratorParamHint;
using sequencer::GeneratorRng;

// The pitch the generator produces at `stepIndex` under `scale`, with a
// FRESH rng carrying `seed` — mirroring the sequencer's per-(pattern, role,
// step) reseeding so identical inputs replay identical outputs. `scale`
// may be null (the no-constraint fallback path).
uint8_t generatedPitchAt(const sequencer::IGenerator& gen, uint8_t patternLength,
                          uint32_t stepIndex, const common::Scale* scale,
                          uint32_t seed) {
    GeneratorRng rng(seed);
    GeneratorContext context(patternLength, scale, rng);
    GeneratorOutput output;
    gen.generate(stepIndex, context, output);
    check(output.hasPitch, "the pitch generator always produces a pitch");
    return output.pitch;
}

// A deterministic per-(template, root, step) seed mix, so the sweep below
// exercises many PRNG streams without any hidden shared state.
uint32_t sweepSeed(int templateIndex, int root, uint32_t step) {
    return static_cast<uint32_t>(templateIndex * 131 + root * 17 + step * 7 + 1u);
}

// --- 1: every generated pitch satisfies the selected scale (FR2/FR4/AC2) ---

void test_every_pitch_satisfies_the_scale() {
    bool factoryRangeInScale = true;
    bool fullRangeInScale = true;
    bool midRangeInScale = true;
    bool fallbackInScale = true;

    for (int t = 0; t < common::scaleTemplateCount(); ++t) {
        for (int root = 0; root < 12; ++root) {
            const common::Scale scale(static_cast<uint8_t>(root),
                                      static_cast<uint8_t>(t));
            const sequencer::ScaleAwarePitchGenerator plugin;
            const sequencer::IGenerator& byBase = plugin;

            // The factory range 48..72.
            for (uint32_t step = 0; step < 32; ++step) {
                const uint8_t pitch =
                    generatedPitchAt(byBase, 16, step, &scale, sweepSeed(t, root, step));
                if (!scale.containsNote(pitch)) factoryRangeInScale = false;
            }

            // The full MIDI range 0..127.
            sequencer::ScaleAwarePitchGenerator full;
            full.configure(0, 127);
            for (uint32_t step = 0; step < 32; ++step) {
                const uint8_t pitch =
                    generatedPitchAt(full, 16, step, &scale, sweepSeed(t, root, step) + 1u);
                if (!scale.containsNote(pitch)) fullRangeInScale = false;
            }

            // A shifted mid range 55..70.
            sequencer::ScaleAwarePitchGenerator mid;
            mid.configure(55, 70);
            for (uint32_t step = 0; step < 32; ++step) {
                const uint8_t pitch =
                    generatedPitchAt(mid, 16, step, &scale, sweepSeed(t, root, step) + 2u);
                if (!scale.containsNote(pitch)) midRangeInScale = false;
            }

            // A degenerate single-note range [61,61]: whenever the scale
            // does not contain pitch class 1 (C#), no in-scale note lies in
            // the range and the TOTAL fallback must still emit an in-scale
            // note (the scale constraint outranks the range).
            sequencer::ScaleAwarePitchGenerator narrow;
            narrow.configure(61, 61);
            for (uint32_t step = 0; step < 8; ++step) {
                const uint8_t pitch =
                    generatedPitchAt(narrow, 16, step, &scale, sweepSeed(t, root, step) + 3u);
                if (!scale.containsNote(pitch)) fallbackInScale = false;
                // Inside the range the pitch must be exactly the one note.
                if (scale.containsNote(61) && pitch != 61) fallbackInScale = false;
            }
        }
    }
    check(factoryRangeInScale,
          "every pitch over the factory range satisfies Scale::containsNote "
          "for every template and root 0-11");
    check(fullRangeInScale,
          "every pitch over the full MIDI range satisfies Scale::containsNote "
          "for every template and root 0-11");
    check(midRangeInScale,
          "every pitch over a shifted mid range satisfies Scale::containsNote "
          "for every template and root 0-11");
    check(fallbackInScale,
          "the no-in-scale-note-in-range fallback still emits an in-scale "
          "pitch for every template and root 0-11");
}

// --- 2: deterministic for a fixed seed --------------------------------------------

void test_deterministic_for_fixed_seed() {
    const common::Scale cMajor(0, 0);
    const sequencer::ScaleAwarePitchGenerator plugin;
    const sequencer::IGenerator& byBase = plugin;

    // Two passes over one ADVANCING rng seeded identically replay the
    // identical 64-step pitch sequence.
    uint8_t first[64];
    uint8_t replay[64];
    for (int pass = 0; pass < 2; ++pass) {
        GeneratorRng rng(0x5EEDu);
        GeneratorContext context(16, &cMajor, rng);
        for (uint32_t step = 0; step < 64; ++step) {
            GeneratorOutput output;
            byBase.generate(step, context, output);
            (pass == 0 ? first : replay)[step] = output.pitch;
        }
    }
    bool identical = true;
    for (uint32_t step = 0; step < 64; ++step) {
        if (first[step] != replay[step]) identical = false;
    }
    check(identical, "a fixed seed replays the identical pitch sequence");

    // A fresh per-step rng with the same seed reproduces the same decision
    // at the same step (the sequencer's reseeding scheme replays exactly).
    bool freshStable = true;
    for (uint32_t step = 0; step < 64; ++step) {
        const uint8_t a = generatedPitchAt(byBase, 16, step, &cMajor, 0x1234u + step);
        const uint8_t b = generatedPitchAt(byBase, 16, step, &cMajor, 0x1234u + step);
        if (a != b) freshStable = false;
    }
    check(freshStable,
          "a fresh identically-seeded rng reproduces the same decision at "
          "the same step");

    // A different seed produces a different pitch somewhere (the PRNG
    // stream actually drives the degree pick).
    bool diverged = false;
    for (uint32_t extra = 1; extra < 16 && !diverged; ++extra) {
        for (uint32_t step = 0; step < 8; ++step) {
            if (generatedPitchAt(byBase, 16, step, &cMajor, 1u) !=
                generatedPitchAt(byBase, 16, step, &cMajor, 1u + extra)) {
                diverged = true;
                break;
            }
        }
    }
    check(diverged, "a different seed produces a different pitch sequence");
}

// --- 3: the range parameter is respected within MIDI 0-127 ------------------------

void test_range_respected() {
    bool allInRange = true;
    bool allInScale = true;

    // Ranges at least 12 semitones wide: every pitch class — hence at least
    // one in-scale note for EVERY scale — falls inside, so the generator
    // never needs the out-of-range fallback and the range must hold exactly.
    const uint8_t lows[] = {0, 12, 23, 36, 47, 60, 71, 84, 95, 105, 116};
    for (const uint8_t low : lows) {
        const uint8_t high = static_cast<uint8_t>((low + 17 > 127) ? 127 : low + 17);
        sequencer::ScaleAwarePitchGenerator plugin;
        plugin.configure(low, high);
        for (int t = 0; t < common::scaleTemplateCount(); ++t) {
            for (int root = 0; root < 12; ++root) {
                const common::Scale scale(static_cast<uint8_t>(root),
                                          static_cast<uint8_t>(t));
                for (uint32_t step = 0; step < 8; ++step) {
                    const uint8_t pitch = generatedPitchAt(
                        plugin, 16, step, &scale, sweepSeed(t, root, step) + low);
                    if (pitch < low || pitch > high) allInRange = false;
                    if (!scale.containsNote(pitch)) allInScale = false;
                }
            }
        }
    }
    check(allInRange, "every generated pitch lies inside the configured "
                      "low/high range for every scale and root");
    check(allInScale, "range-clamped pitches stay in scale");

    // The factory configuration: 48..72.
    const sequencer::ScaleAwarePitchGenerator factory;
    const common::Scale cMajor(0, 0);
    bool factoryInRange = true;
    for (uint32_t step = 0; step < 64; ++step) {
        const uint8_t pitch = generatedPitchAt(factory, 16, step, &cMajor, 7u + step);
        if (pitch < 48 || pitch > 72) factoryInRange = false;
    }
    check(factoryInRange, "the factory configuration spans 48..72");

    // Out-of-range configure() input normalizes: values clamp into 0-127
    // and a low > high pair swaps (total behavior, deterministic).
    sequencer::ScaleAwarePitchGenerator normalized;
    normalized.configure(200, 250);  // both clamp to 127
    check(normalized.lowNote() == 127 && normalized.highNote() == 127,
          "configure() clamps values into MIDI 0-127");
    normalized.configure(200, 5);  // 127 and 5 -> swap to (5, 127)
    check(normalized.lowNote() == 5 && normalized.highNote() == 127,
          "configure() swaps an inverted low/high pair");
    bool normalizedInRange = true;
    for (uint32_t step = 0; step < 32; ++step) {
        const uint8_t pitch =
            generatedPitchAt(normalized, 16, step, &cMajor, 11u + step);
        if (pitch < 5 || pitch > 127) normalizedInRange = false;
    }
    check(normalizedInRange, "the normalized range is respected");
}

// --- 4: capabilities equal PITCH only (AC8) ----------------------------------------

void test_capabilities() {
    const sequencer::ScaleAwarePitchGenerator plugin;
    const GeneratorCapabilities caps = plugin.capabilities();
    check(caps.has(GeneratorCapability::PITCH), "the plugin reports PITCH");
    check(!caps.has(GeneratorCapability::GATE), "the plugin reports no GATE");
    check(!caps.has(GeneratorCapability::VELOCITY),
          "the plugin reports no VELOCITY");
    check(!caps.has(GeneratorCapability::DURATION),
          "the plugin reports no DURATION");
    check(caps.mask() == static_cast<uint8_t>(GeneratorCapability::PITCH),
          "the capability mask is exactly the PITCH bit");
    check(!caps.none(), "the capability set is not empty");

    // generate() resets the output: a caller's stale fields never leak,
    // and the plugin writes ONLY the pitch decision.
    const common::Scale cMajor(0, 0);
    GeneratorRng rng(7u);
    GeneratorContext context(16, &cMajor, rng);
    GeneratorOutput stale;
    stale.hasGate = true;
    stale.gate = true;
    stale.hasVelocity = true;
    stale.velocity = 100;
    stale.hasDuration = true;
    stale.duration = 500;
    plugin.generate(0, context, stale);
    check(stale.hasPitch && !stale.hasGate && !stale.hasVelocity &&
              !stale.hasDuration,
          "generate() clears every field it does not produce");
}

// --- 5: every in-scale note inside the range is reachable --------------------------

void test_all_in_range_scale_notes_reachable() {
    // C major over the factory range 48..72 holds exactly 15 in-scale
    // notes: 48,50,52,53,55,57,59,60,62,64,65,67,69,71,72. Over enough
    // seeds every one of them appears (the degree pick spreads over the
    // WHOLE in-range scale content).
    const common::Scale cMajor(0, 0);
    const sequencer::ScaleAwarePitchGenerator plugin;
    bool seen[128] = {};
    bool onlyExpected = true;
    for (uint32_t seed = 1; seed <= 240; ++seed) {
        const uint8_t pitch = generatedPitchAt(plugin, 16, 0, &cMajor, seed);
        if (pitch < 48 || pitch > 72 || !cMajor.containsNote(pitch)) {
            onlyExpected = false;
        }
        seen[pitch] = true;
    }
    int distinct = 0;
    for (int n = 48; n <= 72; ++n) {
        if (seen[n]) ++distinct;
    }
    check(onlyExpected, "every pitch over the sweep lies in range and scale");
    check(distinct == 15,
          "every one of the 15 in-scale notes of the factory range is "
          "reachable across seeds");
}

// --- 6: a null context scale falls back to the default C-Major ---------------------

void test_null_scale_falls_back_to_default() {
    const sequencer::ScaleAwarePitchGenerator plugin;
    const common::Scale defaultScale;  // root 0, template 0: C major
    bool inDefaultScale = true;
    bool inRange = true;
    for (uint32_t step = 0; step < 64; ++step) {
        const uint8_t pitch = generatedPitchAt(plugin, 16, step, nullptr, 3u + step);
        if (!defaultScale.containsNote(pitch)) inDefaultScale = false;
        if (pitch < 48 || pitch > 72) inRange = false;
    }
    check(inDefaultScale,
          "a null scale falls back to the default C-Major constraint "
          "(total, never out of scale)");
    check(inRange, "the null-scale fallback still respects the range");
}

// --- 7: paramDefs expose the low/high note parameters -------------------------------

void test_param_defs() {
    const sequencer::ScaleAwarePitchGenerator plugin;
    const sequencer::GeneratorParamSet& defs = plugin.paramDefs();

    check(defs.count == 2, "the plugin declares exactly two parameters");
    check(defs.count <= sequencer::kMaxGeneratorParams,
          "the parameter count stays within the fixed capacity");

    check(defs.defs[0].id == sequencer::ScaleAwarePitchGenerator::kParamLowNote &&
              defs.defs[1].id == sequencer::ScaleAwarePitchGenerator::kParamHighNote,
          "parameter ids are lowNote(0), highNote(1)");
    for (uint8_t i = 0; i < defs.count; ++i) {
        check(defs.defs[i].name != nullptr && defs.defs[i].name[0] != '\0',
              "parameter names are non-empty");
        check(defs.defs[i].minValue <= defs.defs[i].maxValue,
              "parameter ranges are ordered");
        check(defs.defs[i].defaultValue >= defs.defs[i].minValue &&
                  defs.defs[i].defaultValue <= defs.defs[i].maxValue,
              "parameter defaults lie inside their ranges");
        check(defs.defs[i].hint == GeneratorParamHint::Note,
              "both parameters present as MIDI note numbers");
    }
    for (uint8_t i = 0; i < defs.count; ++i)
        for (uint8_t j = i + 1; j < defs.count; ++j)
            check(defs.defs[i].id != defs.defs[j].id, "parameter ids are unique");

    check(std::strcmp(defs.defs[0].name, "Low Note") == 0, "Low Note name");
    check(defs.defs[0].minValue == 0 && defs.defs[0].maxValue == 127 &&
              defs.defs[0].defaultValue == 48,
          "Low Note range 0..127, default 48");
    check(std::strcmp(defs.defs[1].name, "High Note") == 0, "High Note name");
    check(defs.defs[1].minValue == 0 && defs.defs[1].maxValue == 127 &&
              defs.defs[1].defaultValue == 72,
          "High Note range 0..127, default 72");
}

// --- 8: identity + configuration accessors -----------------------------------------

void test_identity_and_config() {
    const sequencer::ScaleAwarePitchGenerator plugin;
    check(std::strcmp(plugin.id(), "builtin.pitch") == 0,
          "the plugin id is builtin.pitch");
    check(plugin.name() != nullptr && plugin.name()[0] != '\0',
          "the display name is non-empty");

    // Factory defaults: the two-octave span 48..72.
    check(plugin.lowNote() == 48 && plugin.highNote() == 72,
          "the default configuration is (48, 72)");

    const sequencer::ScaleAwarePitchGenerator configured(10, 90);
    check(configured.lowNote() == 10 && configured.highNote() == 90,
          "the constructor stores the configuration");

    sequencer::ScaleAwarePitchGenerator reconfigured;
    reconfigured.configure(21, 108);
    check(reconfigured.lowNote() == 21 && reconfigured.highNote() == 108,
          "configure() stores the configuration");

    // The id fits the fixed generator-id storage: assignable to the PITCH
    // role of a pattern's generative state.
    sequencer::PatternGenerativeState assigned;
    check(assigned.setGeneratorId(sequencer::GeneratorRole::PITCH, plugin.id()),
          "the plugin id fits the fixed generator id storage");
    check(assigned.hasGenerator(sequencer::GeneratorRole::PITCH),
          "the pitch role accepts the plugin id");
}

// --- 9: registration (AC1: RegisterGenerators.cpp is the only listing) -------------

void test_registration() {
    sequencer::GeneratorRegistry registry;
    sequencer::registerGenerators(registry);

    check(registry.count() == 2,
          "registerGenerators registers the Euclidean gate and the "
          "scale-aware pitch generator");
    const sequencer::IGenerator* found = registry.find("builtin.pitch");
    check(found != nullptr, "builtin.pitch is reachable by id");
    check(found != nullptr &&
              found->capabilities().has(GeneratorCapability::PITCH),
          "the registered pitch generator declares PITCH");
    check(found != nullptr && found->paramDefs().count == 2,
          "the registered pitch generator exposes its two parameters");
    check(registry.find("builtin.euclidean") != nullptr,
          "the Euclidean gate generator stays registered");

    // The registered instance carries the FACTORY configuration: its
    // decisions match a locally factory-configured instance bit for bit.
    const sequencer::ScaleAwarePitchGenerator factory;
    const common::Scale cMajor(0, 0);
    bool matches = true;
    for (uint32_t step = 0; step < 32; ++step) {
        if (generatedPitchAt(*found, 16, step, &cMajor, 77u + step) !=
            generatedPitchAt(factory, 16, step, &cMajor, 77u + step)) {
            matches = false;
        }
    }
    check(matches, "the registered instance uses the factory configuration");

    // Repeated registration is safe: the duplicates are rejected.
    sequencer::registerGenerators(registry);
    check(registry.count() == 2, "a repeated registerGenerators adds nothing");

    // The not-yet-registered built-ins stay unreachable.
    check(registry.find("builtin.probability") == nullptr,
          "not-yet-registered built-ins stay unreachable");
}

}  // namespace

int main() {
    test_every_pitch_satisfies_the_scale();
    test_deterministic_for_fixed_seed();
    test_range_respected();
    test_capabilities();
    test_all_in_range_scale_notes_reachable();
    test_null_scale_falls_back_to_default();
    test_param_defs();
    test_identity_and_config();
    test_registration();

    if (failures == 0) {
        std::printf("test_scale_pitch_generator: all checks passed\n");
        return 0;
    }
    std::printf("test_scale_pitch_generator: %d check(s) failed\n", failures);
    return 1;
}
