#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "../IMutationOperator.h"
#include "CompositeMutations.h"   // the T10 composite operators
#include "ParameterMutations.h"   // the T9 parameter operators
#include "TransformMutations.h"   // the T9 transform operators

// Fixed-capacity mutation operator registry (generative-engine-expansion
// task 10, FR5, FR8).
//
// MutationOperatorRegistry is the mutation-side sibling of
// GeneratorRegistry: a statically allocated, fixed-capacity array of
// IMutationOperator* the UI lists (id() / name() / supportedTargets(),
// restricted to the operator's targets per FR8 operator control) and the
// command path dispatches through by id (task 17). Concrete operators live
// in src/sequencer/mutations/, are listed ONLY in
// registerBuiltInMutations() below, and are held by pointer: the registry
// never copies, owns or allocates them.
//
// registerBuiltInMutations() registers every T9 and T10 operator — the
// five parameter mutations, the three transform mutations and the two
// composites — from one shared, statically allocated bank (an inline
// variable: no heap, no first-call guard; the instances have static
// storage duration like the built-in generator bank in
// generators/RegisterGenerators.cpp). Repeated calls stay safe through
// add()'s duplicate guard.
//
// Global constraints honored here: fixed footprint — no heap, no locks, no
// growth; registration happens once at startup, the mutation path
// performs read-only lookups only. SDK-free: <cstdint>/<cstring> plus the
// SDK-free operator headers only, so this compiles unchanged in the
// firmware and the host test build.

namespace sequencer {

class MutationOperatorRegistry {
public:
    // Fixed capacity: the ten built-in operators (T9's eight + T10's two
    // composites) plus headroom for future plugins. The registry NEVER
    // grows.
    static constexpr uint8_t MAX_OPERATORS = 16;

    // Register one operator. Fails (returns false) WITHOUT modifying the
    // registry when: the pointer is null, the id is null, the capacity is
    // exhausted, or the id is already taken (duplicates are rejected so a
    // repeated registerBuiltInMutations() call stays safe).
    bool add(IMutationOperator* op) {
        if (op == nullptr || count_ >= MAX_OPERATORS) return false;
        const char* id = op->id();
        if (id == nullptr) return false;
        for (uint8_t i = 0; i < count_; ++i) {
            const char* existing = entries_[i]->id();
            if (existing != nullptr && std::strcmp(existing, id) == 0) return false;
        }
        entries_[count_] = op;
        ++count_;
        return true;
    }

    // Lookup by id, or nullptr when no registered operator matches (a null
    // id also misses).
    IMutationOperator* find(const char* id) const {
        if (id == nullptr) return nullptr;
        for (uint8_t i = 0; i < count_; ++i) {
            const char* existing = entries_[i]->id();
            if (existing != nullptr && std::strcmp(existing, id) == 0) return entries_[i];
        }
        return nullptr;
    }

    // Number of registered operators (bounded by MAX_OPERATORS).
    uint8_t count() const { return count_; }

    // Registration-order iteration for the UI's operator list; nullptr at
    // or past the count (and for an empty registry), so callers need no
    // separate bounds check.
    IMutationOperator* at(uint8_t index) const {
        return (index < count_) ? entries_[index] : nullptr;
    }

    // Remove every registration (factory reset; the operators themselves,
    // owned elsewhere, are untouched).
    void clear() { count_ = 0; }

private:
    IMutationOperator* entries_[MAX_OPERATORS] = {};
    uint8_t count_ = 0;
};

// The process-wide mutation operator registry: statically allocated (an
// inline variable — no heap, no first-call guard), populated once at
// startup by registerBuiltInMutations() and read-only afterwards
// (mirrors g_generatorRegistry).
inline MutationOperatorRegistry g_mutationRegistry{};

// The registered built-in operators: one shared statically allocated bank
// (inline variable, static storage duration — no heap, no per-call
// construction). The two composites each embed their own by-value T9
// sub-operator banks, so every operator here is self-contained.
struct BuiltInMutationOperators {
    DensityMutation density;
    PitchSetMutation pitchSet;
    VelocitySetMutation velocitySet;
    DurationMutation duration;
    EuclideanParamMutation euclidean;
    RotateMutation rotate;
    InvertMutation invert;
    ShuffleMutation shuffle;
    MutateAllMutation mutateAll;
    EvolveMutation evolve;
};

inline BuiltInMutationOperators g_builtinMutationOperators;

// Register every built-in mutation operator — the T9 parameter mutations
// (builtin.mutate-density / -pitch-set / -velocity-set / -duration /
// -euclidean), the T9 transform mutations (builtin.rotate / invert /
// shuffle) and the T10 composites (builtin.mutate-all / builtin.evolve) —
// the ONLY place built-in mutation operators are listed. Repeated calls
// are safe: the duplicate guard rejects the already-registered ids
// without disturbing the registry.
inline void registerBuiltInMutations(MutationOperatorRegistry& registry) {
    registry.add(&g_builtinMutationOperators.density);
    registry.add(&g_builtinMutationOperators.pitchSet);
    registry.add(&g_builtinMutationOperators.velocitySet);
    registry.add(&g_builtinMutationOperators.duration);
    registry.add(&g_builtinMutationOperators.euclidean);
    registry.add(&g_builtinMutationOperators.rotate);
    registry.add(&g_builtinMutationOperators.invert);
    registry.add(&g_builtinMutationOperators.shuffle);
    registry.add(&g_builtinMutationOperators.mutateAll);
    registry.add(&g_builtinMutationOperators.evolve);
}

// Size bounds on the new state (global constraint): the whole registry is
// one small static array of pointers; the bank is ten small fixed objects.
static_assert(std::is_trivially_copyable<MutationOperatorRegistry>::value,
              "MutationOperatorRegistry must be trivially copyable");
static_assert(std::is_standard_layout<MutationOperatorRegistry>::value,
              "MutationOperatorRegistry must be standard layout");
static_assert(sizeof(MutationOperatorRegistry) <= 160,
              "MutationOperatorRegistry stays a small fixed POD");
static_assert(MutationOperatorRegistry::MAX_OPERATORS >= 10,
              "the fixed capacity holds every built-in operator");

}  // namespace sequencer
