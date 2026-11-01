#pragma once

#include <cstdint>

#include "../common/PatternId.h"
#include "../common/pitch_set.h"  // common::PlayingOrder (header-only enum)
#include "../common/GenerationParameters.h"  // common::GenerationParameters (POD)

// Fixed-capacity Pattern snapshot for core1 command handling (performance-safe-
// foundation task 6.4, Requirements 7.4, 7.9, 7.10).
//
// The current core1 receive path (commands::receiveCommand) decodes gate/pitch/
// velocity payloads into std::vector members and calls .assign(), which HEAP-
// ALLOCATES on the timing-critical core1 while sequencing. Req 7.9 forbids any
// steady-state allocation on the receive/scheduling/application paths. This
// snapshot is the allocation-free replacement: a POD, fixed-capacity container
// core1 can reuse in place (no construction, no growth, no free) to carry one
// pattern's complete material from the inbox to application.
//
// Capacities match the existing wire protocol limits (commands::MAX_* ) so a
// snapshot can hold any legal payload without truncation:
//   - gates   : up to kMaxGateSteps boolean steps, stored as a packed bitset.
//   - pitches : up to kMaxSetLen bytes.
//   - velocities: up to kMaxSetLen bytes.
//
// Bounded per-cycle draining (Req 7.4) is provided by CommandInbox::drain; this
// type is what each drained command decodes INTO, reusing one buffer, so the
// whole receive->apply path is heap-free. SDK-free: only <cstdint> and the
// header-only canonical id / PlayingOrder enum, so it compiles in firmware and
// host tests identically. No logging lives here (Req 7.10).

namespace sequencer {

class PatternSnapshot {
public:
    // Protocol caps (mirror commands::MAX_GATE_SET_LENGTH / MAX_PITCH_SET_LENGTH).
    static constexpr uint16_t kMaxGateSteps = 64 * 96;  // 6144
    static constexpr uint8_t kMaxSetLen = 16;
    static constexpr uint16_t kGateWords = (kMaxGateSteps + 31) / 32;

    PatternSnapshot() = default;

    void clear() {
        target = common::PatternId{};
        gateCount_ = 0;
        pitchCount_ = 0;
        velocityCount_ = 0;
        pitchOrder_ = common::PlayingOrder::FORWARDS;
        velocityOrder_ = common::PlayingOrder::FORWARDS;
        genParams_ = common::GenerationParameters{};  // back to behavior-preserving
        for (uint16_t i = 0; i < kGateWords; ++i) gateBits_[i] = 0;
    }

    // --- Gates (packed bitset) -----------------------------------------------
    void setGateCount(uint16_t count) {
        gateCount_ = count > kMaxGateSteps ? kMaxGateSteps : count;
    }
    uint16_t gateCount() const { return gateCount_; }
    void setGate(uint16_t index, bool on) {
        if (index >= kMaxGateSteps) return;
        const uint16_t word = index >> 5;
        const uint32_t bit = 1u << (index & 31);
        if (on) gateBits_[word] |= bit;
        else gateBits_[word] &= ~bit;
    }
    bool gate(uint16_t index) const {
        if (index >= gateCount_) return false;
        return (gateBits_[index >> 5] >> (index & 31)) & 1u;
    }

    // --- Pitches -------------------------------------------------------------
    void setPitchCount(uint8_t count) {
        pitchCount_ = count > kMaxSetLen ? kMaxSetLen : count;
    }
    uint8_t pitchCount() const { return pitchCount_; }
    void setPitch(uint8_t index, uint8_t note) {
        if (index < kMaxSetLen) pitches_[index] = note & 0x7F;
    }
    uint8_t pitch(uint8_t index) const { return index < pitchCount_ ? pitches_[index] : 0; }
    void setPitchOrder(common::PlayingOrder o) { pitchOrder_ = o; }
    common::PlayingOrder pitchOrder() const { return pitchOrder_; }

    // --- Velocities ----------------------------------------------------------
    void setVelocityCount(uint8_t count) {
        velocityCount_ = count > kMaxSetLen ? kMaxSetLen : count;
    }
    uint8_t velocityCount() const { return velocityCount_; }
    void setVelocity(uint8_t index, uint8_t vel) {
        if (index < kMaxSetLen) velocities_[index] = vel & 0x7F;
    }
    uint8_t velocity(uint8_t index) const {
        return index < velocityCount_ ? velocities_[index] : 0;
    }
    void setVelocityOrder(common::PlayingOrder o) { velocityOrder_ = o; }
    common::PlayingOrder velocityOrder() const { return velocityOrder_; }

    // --- Generation Parameters (generative-engine-expansion task 6.1) --------
    // A POD GenerationParameters rides the snapshot so a parameter edit reaches
    // core1 through the same allocation-free handoff as gate/pitch/velocity.
    void setGenerationParameters(const common::GenerationParameters& gp) { genParams_ = gp; }
    const common::GenerationParameters& generationParameters() const { return genParams_; }

    common::PatternId target{};  // which pattern this snapshot applies to

    // Documented fixed memory footprint (bytes), for the handoff budget (Req 7.2).
    static constexpr uint32_t footprintBytes() {
        return kGateWords * 4u + kMaxSetLen * 2u + 16u +
               static_cast<uint32_t>(sizeof(common::GenerationParameters));
    }

private:
    uint32_t gateBits_[kGateWords] = {0};
    uint16_t gateCount_ = 0;
    uint8_t pitches_[kMaxSetLen] = {0};
    uint8_t velocities_[kMaxSetLen] = {0};
    uint8_t pitchCount_ = 0;
    uint8_t velocityCount_ = 0;
    common::PlayingOrder pitchOrder_ = common::PlayingOrder::FORWARDS;
    common::PlayingOrder velocityOrder_ = common::PlayingOrder::FORWARDS;
    common::GenerationParameters genParams_{};  // defaults to behavior-preserving
};

}  // namespace sequencer
