// generative-engine-expansion Property 8: per-Pattern generation determinism.
//
// Validates Requirements 4.1, 4.2, 4.3, 4.5: the per-Pattern generation random
// state (sequencer::PatternRng) reproduces the SAME sequence of draws from the
// same seed, so a seeded Pattern replays identically across runs and across a
// save/load round-trip (the seed is what is persisted, not the stream). Also
// checks the draw ranges the lens relies on: rollPercent in 0..99, signedRange
// in [-r,+r].
//
// Shared-runner arrangement (see test/test_main.cpp): this file defines
// runProperty8() and NO main(); the shared runner aggregates it.

#include "PatternRng.h"

#include <rapidcheck.h>

#include <cstdint>
#include <vector>

using sequencer::PatternRng;

bool runProperty8() {
    bool ok = true;

    // Determinism: two RNGs seeded with the same value produce identical draw
    // sequences (Req 4.2/4.3). This is the core reproducibility guarantee.
    ok &= rc::check(
        "Property 8a: same seed -> identical draw sequence",
        [](uint32_t seed, uint8_t nDraws) {
            PatternRng a, b;
            a.seed(seed);
            b.seed(seed);
            for (uint8_t i = 0; i < nDraws; ++i) {
                RC_ASSERT(a.next() == b.next());
            }
        });

    // A save/load round-trip persists the SEED, not the live state; re-seeding a
    // fresh RNG with the stored seed reproduces the same material from the start
    // (Req 4.3). Model: draw N, then "reload" by seeding a new RNG and draw N.
    ok &= rc::check(
        "Property 8b: re-seeding from the stored seed reproduces the sequence",
        [](uint32_t seed, uint8_t nDraws) {
            PatternRng live;
            live.seed(seed);
            std::vector<uint32_t> first;
            for (uint8_t i = 0; i < nDraws; ++i) first.push_back(live.next());

            PatternRng reloaded;  // simulates boot + load of the saved seed
            reloaded.seed(seed);
            for (uint8_t i = 0; i < nDraws; ++i) {
                RC_ASSERT(reloaded.next() == first[i]);
            }
        });

    // Different seeds should (overwhelmingly) diverge within a few draws — guards
    // against a seeding function that collapses distinct seeds to one state.
    ok &= rc::check(
        "Property 8c: distinct seeds diverge",
        [](uint32_t s1, uint32_t s2) {
            RC_PRE(s1 != s2);
            PatternRng a, b;
            a.seed(s1);
            b.seed(s2);
            bool diverged = false;
            for (int i = 0; i < 8; ++i) {
                if (a.next() != b.next()) { diverged = true; break; }
            }
            RC_ASSERT(diverged);
        });

    // rollPercent stays in 0..99 (so P==100 always sounds, P==0 never does).
    ok &= rc::check(
        "Property 8d: rollPercent in [0,99]",
        [](uint32_t seed, uint8_t nDraws) {
            PatternRng r;
            r.seed(seed);
            for (uint8_t i = 0; i < nDraws; ++i) {
                const uint8_t p = r.rollPercent();
                RC_ASSERT(p <= 99);
            }
        });

    // signedRange(r) stays within [-r, +r] for every r.
    ok &= rc::check(
        "Property 8e: signedRange within [-r,+r]",
        [](uint32_t seed, uint8_t r, uint8_t nDraws) {
            PatternRng rng;
            rng.seed(seed);
            for (uint8_t i = 0; i < nDraws; ++i) {
                const int v = rng.signedRange(r);
                RC_ASSERT(v >= -static_cast<int>(r));
                RC_ASSERT(v <= static_cast<int>(r));
            }
        });

    return ok;
}
