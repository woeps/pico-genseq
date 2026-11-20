#pragma once

// generative-engine-expansion task 4.1 — the per-Pattern generation random
// state.
//
// One PatternRng per Pattern runtime is the single source of all stochastic
// behavior for that Pattern when a Deterministic Seed is set (Req 4.1): RANDOM
// playing-order position, Step Probability rolls, and Velocity Humanization all
// draw from it, so a seed reproduces the WHOLE Pattern, not one stream.
//
// It is a named LCG (same multiplier/increment family as the existing per-stream
// LCGs in PitchSet/VelocitySet) so its statistical behavior matches what the
// streams already use. SDK-free, header-only, allocation-free — safe on the core1
// Hot Path (Req 6.1).
//
// IMPORTANT (design §3): when NO seed is set, the sequencer does NOT draw RANDOM
// order from this RNG — it leaves the per-stream LCGs untouched so RANDOM output
// stays byte-identical to pre-milestone (property2_sequencer_equivalence). This
// type only carries the mechanism; the seeded/unseeded policy lives in the
// sequencer.

#include <cstdint>

namespace sequencer {

class PatternRng {
public:
    PatternRng() : state_(kDefaultSeed) {}

    // Reset the state to a defined function of the seed (Req 4.2). A non-zero
    // mixing constant avoids a zero state (which an LCG with increment 12345
    // would still escape, but keeping it non-trivial makes small seeds diverge
    // quickly).
    void seed(uint32_t s) { state_ = (s ^ 0x9E3779B9u) * 2654435761u + 1u; }

    // Raw LCG step (same family as PitchSet/VelocitySet::lcgNext). Returns the
    // next 32-bit state so callers can derive bytes or ranges from it.
    uint32_t next() {
        state_ = state_ * 1103515245u + 12345u;
        return state_;
    }

    // A byte 0..255 drawn from the high bits (matches the stream LCGs' byte use).
    uint8_t nextByte() { return static_cast<uint8_t>((next() >> 16) & 0xFFu); }

    // Uniform percent 0..99 for a probability roll: a gated step SOUNDS when
    // rollPercent() < P (so P==100 always sounds, P==0 never does).
    uint8_t rollPercent() { return static_cast<uint8_t>(nextByte() * 100u / 256u); }

    // Uniform signed value in [-r, +r] for velocity humanization. r==0 yields 0
    // with no draw perturbation effect (caller should skip when range is 0).
    int8_t signedRange(uint8_t r) {
        if (r == 0) return 0;
        const uint16_t span = static_cast<uint16_t>(2u * r + 1u);  // [-r .. +r]
        const int v = static_cast<int>(nextByte() % span) - static_cast<int>(r);
        return static_cast<int8_t>(v);
    }

    // A position 0..size-1 for a seeded RANDOM-order override.
    uint8_t position(uint8_t size) {
        return size == 0 ? 0 : static_cast<uint8_t>(nextByte() % size);
    }

    uint32_t state() const { return state_; }

private:
    static constexpr uint32_t kDefaultSeed = 0xC0FFEEu;  // documented free-running default
    uint32_t state_;
};

}  // namespace sequencer
