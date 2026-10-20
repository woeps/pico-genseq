#pragma once

#include <vector>

#include "PatternSnapshot.h"
#include "../common/pattern.h"
#include "../common/gate_set.h"
#include "../common/pitch_set.h"
#include "../common/velocity_set.h"

// Atomic complete-Pattern-snapshot application (performance-safe-foundation
// task 7.4, Requirements 5.7, 6.8, 7.1).
//
// When a committed set-replacement operation reaches its boundary, the ENTIRE
// pattern material (gate + pitch + velocity) must be swapped in ONE atomic step
// so the scheduler never observes a half-applied pattern — e.g. a new gate set
// paired with the old pitch set (Req 6.8, 7.1). And the currently sounding note
// must complete using its ORIGINAL recorded lifecycle; the replacement controls
// only the NEXT note (Req 5.7).
//
// applyPatternSnapshot() builds all three replacement sets fully FIRST, then
// assigns them to the Pattern back-to-back. The scheduler services a pattern
// inside a single serviceTick() (it never yields mid-pattern), so a boundary
// application that runs between ticks is atomic from the tick path's view: a
// tick sees either the whole old material or the whole new material, never a mix.
//
// Sounding-note preservation is structural, not something this applier must do:
// NoteLifecycle owns the sounding note by its recorded (channel,note) identity
// (task 2.3/2.4), independent of the pattern's material, so replacing the sets
// here does NOT release or retarget the note already playing — it finishes on
// its own falling edge, and the new material drives the next onset (Req 5.7).
//
// Runtime positions are deliberately NOT reset here: the pattern keeps its phase
// across a set replacement so the swap is seamless on the shared timeline.

namespace sequencer {

inline void applyPatternSnapshot(common::Pattern& pattern, const PatternSnapshot& snap) {
    // Build the complete replacement material FIRST (no partial state is ever
    // written to the live pattern during construction).
    std::vector<bool> gates(snap.gateCount());
    for (uint16_t i = 0; i < snap.gateCount(); ++i) gates[i] = snap.gate(i);

    std::vector<uint8_t> pitches(snap.pitchCount());
    for (uint8_t i = 0; i < snap.pitchCount(); ++i) pitches[i] = snap.pitch(i);

    std::vector<uint8_t> velocities(snap.velocityCount());
    for (uint8_t i = 0; i < snap.velocityCount(); ++i) velocities[i] = snap.velocity(i);

    common::GateSet gateSet(gates);
    common::PitchSet pitchSet(pitches, snap.pitchOrder());
    common::VelocitySet velocitySet(velocities, snap.velocityOrder());

    // Atomic swap: assign all three complete sets back-to-back. Between ticks the
    // scheduler cannot observe a partially-applied pattern (Req 6.8/7.1). The
    // sounding note is untouched (owned by NoteLifecycle, Req 5.7).
    pattern.setGateSet(gateSet);
    pattern.setPitchSet(pitchSet);
    pattern.setVelocitySet(velocitySet);
}

}  // namespace sequencer
