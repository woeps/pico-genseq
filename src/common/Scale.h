#pragma once

// generative-engine-expansion task 2.1 — the scale model library.
//
// SDK-free free functions that turn a (ScaleId, root) pair into a 12-bit
// pitch-class membership mask and resolve an arbitrary candidate pitch onto an
// allowed pitch per a QuantizeMode. This is the SINGLE source of truth for
// "which pitches are in scale": the sequencer calls it at emission (Req 1) and
// the PitchSet view calls the SAME functions for its membership cue (Req 8.4),
// so the view and the audible result can never disagree.
//
// Only <cstdint> and the header-only GenerationParameters enums — no std::vector,
// no heap, no Pico SDK — so it is allocation-free on the Hot Path (Req 6.1) and
// compiles identically in firmware and host tests.

#include <cstdint>

#include "GenerationParameters.h"  // common::ScaleId, common::QuantizeMode

namespace common {
namespace scale {

// A 12-bit membership mask for one octave of pitch classes. Bit i (0..11) set
// means pitch class i is in scale. CHROMATIC yields 0x0FFF (all set).
//
// `root` is a pitch class 0..11; values outside that range are reduced mod 12.
// The returned mask is rotated so bit (root) is the tonic.
uint16_t membershipMask(ScaleId id, uint8_t root);

// Is MIDI note `n` (0..127) in the scale described by `mask`? Tests only the
// pitch class (n % 12), so it is octave-independent.
bool inScale(uint16_t mask, uint8_t n);

// Resolve MIDI note `n` to an in-scale note per `mode`, writing the result to
// `out` and returning true. Returns false ONLY for SKIP when `n` is out of
// scale (the caller then suppresses the step, Req 1.4). For NEAREST/DOWN/UP the
// search spans at most 12 semitones and always finds an allowed pitch (every
// non-empty scale has a member within an octave), clamping to 0..127 at the
// MIDI range edges. When `n` is already in scale, `out == n` and the result is
// true for every mode. The octave of `n` is preserved except where DOWN/UP must
// cross into an adjacent octave to find an allowed pitch (Req 1.2).
bool quantize(uint16_t mask, QuantizeMode mode, uint8_t n, uint8_t& out);

}  // namespace scale
}  // namespace common
