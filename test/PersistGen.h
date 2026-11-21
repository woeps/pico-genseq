#pragma once

// RapidCheck generators for VALID v3 persistence configs (separate-velocity-set-gate-set-and-
// note-set): random library occupancy, random set contents inside the documented ranges, and
// per-pattern references that always point at an occupied slot (so patterns may share sets).

#include "persistence/PersistableConfig.h"

#include <rapidcheck.h>

#include <cstdint>
#include <vector>

namespace persist_gen {

inline persistence::GateSetRecord genGateSet(bool fat) {
    persistence::GateSetRecord g{};
    g.algorithm = 0;
    const uint8_t steps = fat ? 64 : *rc::gen::inRange<int>(1, 65);
    g.steps = steps;
    g.pulses = *rc::gen::inRange<int>(0, steps + 1);
    g.rotation = *rc::gen::inRange<int>(0, steps);
    g.noteLength = *rc::gen::inRange<int>(0, 6);
    g.gateLength = *rc::gen::inRange<int>(0, 101);
    g.patternProbability = *rc::gen::inRange<int>(0, 101);
    g.stepProbCount = fat ? persistence::MAX_SET_LEN
                          : *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);
    for (uint8_t i = 0; i < g.stepProbCount; ++i) {
        g.stepProbability[i] = *rc::gen::weightedOneOf<uint8_t>(
            {{1, rc::gen::just<uint8_t>(0xFF)},
             {3, rc::gen::cast<uint8_t>(rc::gen::inRange<int>(0, 101))}});
    }
    g.gateStepCount = fat ? persistence::GateSetRecord::kMaxGateEditSteps
                          : *rc::gen::inRange<int>(0, persistence::GateSetRecord::kMaxGateEditSteps + 1);
    for (uint8_t i = 0; i < g.gateStepCount; ++i) g.gateStates[i] = *rc::gen::inRange<int>(0, 2);
    return g;
}

inline persistence::NoteSetRecord genNoteSet(bool fat) {
    persistence::NoteSetRecord n{};
    n.count = fat ? persistence::MAX_SET_LEN : *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);
    n.order = *rc::gen::inRange<int>(0, 4);
    for (uint8_t i = 0; i < n.count; ++i) n.pitches[i] = *rc::gen::inRange<int>(0, 128);
    n.scale = *rc::gen::inRange<int>(0, 3);
    n.scaleRoot = *rc::gen::inRange<int>(0, 12);
    n.quantize = *rc::gen::inRange<int>(0, 4);
    return n;
}

inline persistence::VelocitySetRecord genVelocitySet(bool fat) {
    persistence::VelocitySetRecord v{};
    v.count = fat ? persistence::MAX_SET_LEN : *rc::gen::inRange<int>(0, persistence::MAX_SET_LEN + 1);
    v.order = *rc::gen::inRange<int>(0, 4);
    for (uint8_t i = 0; i < v.count; ++i) v.velocities[i] = *rc::gen::inRange<int>(0, 128);
    v.humanizeRange = *rc::gen::inRange<int>(0, 64);
    v.seedSet = *rc::gen::inRange<int>(0, 2);
    v.seed = *rc::gen::arbitrary<uint32_t>();
    v.coupling = *rc::gen::inRange<int>(0, 2);
    return v;
}

// A non-empty occupancy mask over SET_LIBRARY_SIZE slots (all slots when `full`).
inline uint16_t genMask(bool full) {
    if (full) return 0xFFFF;
    return static_cast<uint16_t>(*rc::gen::inRange<int>(1, 0x10000));
}

inline uint8_t pickOccupied(uint16_t mask) {
    std::vector<uint8_t> slots;
    for (uint8_t i = 0; i < persistence::SET_LIBRARY_SIZE; ++i) {
        if ((mask >> i) & 1u) slots.push_back(i);
    }
    return slots[*rc::gen::inRange<std::size_t>(0, slots.size())];
}

// minPatterns..MAX_PATTERNS patterns. `fat` fills every library slot at maximum size, the
// worst-case record.
inline persistence::PersistableConfig genConfig(int minPatterns = 0, bool fat = false) {
    persistence::PersistableConfig cfg{};
    cfg.patternCount = *rc::gen::inRange<int>(minPatterns, persistence::MAX_PATTERNS + 1);
    cfg.bpm = *rc::gen::inRange<int>(0, 1001);
    cfg.midiClockEnabled = *rc::gen::inRange<int>(0, 2);
    cfg.gateOccupied = genMask(fat);
    cfg.noteOccupied = genMask(fat);
    cfg.velocityOccupied = genMask(fat);
    for (uint8_t s = 0; s < persistence::SET_LIBRARY_SIZE; ++s) {
        if ((cfg.gateOccupied >> s) & 1u) cfg.gateSets[s] = genGateSet(fat);
        if ((cfg.noteOccupied >> s) & 1u) cfg.noteSets[s] = genNoteSet(fat);
        if ((cfg.velocityOccupied >> s) & 1u) cfg.velocitySets[s] = genVelocitySet(fat);
    }
    for (uint8_t i = 0; i < cfg.patternCount; ++i) {
        cfg.patterns[i].active = *rc::gen::inRange<int>(0, 2);
        cfg.patterns[i].midiChannel = *rc::gen::inRange<int>(1, 17);
        cfg.patterns[i].gateRef = pickOccupied(cfg.gateOccupied);
        cfg.patterns[i].noteRef = pickOccupied(cfg.noteOccupied);
        cfg.patterns[i].velocityRef = pickOccupied(cfg.velocityOccupied);
    }
    return cfg;
}

}  // namespace persist_gen
