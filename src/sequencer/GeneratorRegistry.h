#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "GeneratorCapabilities.h"
#include "IGenerator.h"

// Statically allocated generator registry (generative-engine-expansion
// task 2, FR1, AC1).
//
// A fixed-capacity array of IGenerator* — the ONLY coupling point between
// the core sequencer and the generator plugins: the sequencer dispatches by
// id through this registry and never names a concrete generator type, so a
// new plugin is added by implementing IGenerator and registering it in
// generators/RegisterGenerators.cpp alone (AC1).
//
// Registration happens once at startup (registerGenerators); the tick path
// performs read-only lookups only. Fixed footprint: no heap, no locks, no
// growth. The process-wide instance g_generatorRegistry mirrors the
// modulation registry; tests may instead instantiate local registries.
// SDK-free: <cstdint>/<cstring> only, no Pico SDK types.

namespace sequencer {

class GeneratorRegistry {
public:
    // Fixed capacity (kMaxGeneratorParams' sibling limit in
    // GeneratorCapabilities.h): the four built-in plugins (T4-T7) plus
    // headroom. The registry NEVER grows.
    static constexpr uint8_t MAX_GENERATORS = kMaxGenerators;

    // Register one generator. Fails (returns false) WITHOUT modifying the
    // registry when: the pointer is null, the id is null, the capacity is
    // exhausted, or the id is already taken (duplicates are rejected so a
    // repeated registerGenerators() call stays safe).
    bool add(IGenerator* generator) {
        if (generator == nullptr || count_ >= MAX_GENERATORS) return false;
        const char* id = generator->id();
        if (id == nullptr) return false;
        for (uint8_t i = 0; i < count_; ++i) {
            const char* existing = entries_[i]->id();
            if (existing != nullptr && std::strcmp(existing, id) == 0) return false;
        }
        entries_[count_] = generator;
        ++count_;
        return true;
    }

    // Lookup by id, or nullptr when no registered generator matches (a null
    // id also misses).
    IGenerator* find(const char* id) const {
        if (id == nullptr) return nullptr;
        for (uint8_t i = 0; i < count_; ++i) {
            const char* existing = entries_[i]->id();
            if (existing != nullptr && std::strcmp(existing, id) == 0) return entries_[i];
        }
        return nullptr;
    }

    // Number of registered generators (bounded by MAX_GENERATORS).
    uint8_t count() const { return count_; }

    // Registration-order iteration; nullptr at or past the count (and at 0
    // for an empty registry), so callers need no separate bounds check.
    IGenerator* at(uint8_t index) const {
        return (index < count_) ? entries_[index] : nullptr;
    }

    // Remove every registration (factory reset; the generators themselves,
    // owned elsewhere, are untouched).
    void clear() { count_ = 0; }

private:
    IGenerator* entries_[MAX_GENERATORS] = {};
    uint8_t count_ = 0;
};

// The process-wide generator registry: statically allocated (an inline
// variable — no heap, no first-call guard), populated once at startup by
// registerGenerators() and read-only afterwards.
inline GeneratorRegistry g_generatorRegistry{};

// Declared here, defined in generators/RegisterGenerators.cpp: the ONLY
// place built-in generators are listed (AC1). It currently registers none;
// tasks T4-T7 add the built-in plugins there. Repeated calls are safe
// through add()'s duplicate guard.
void registerGenerators(GeneratorRegistry& registry);

// Size bounds on the new state (global constraint): the whole registry is
// one small static array of pointers.
static_assert(std::is_trivially_copyable<GeneratorRegistry>::value,
              "GeneratorRegistry must be trivially copyable");
static_assert(sizeof(GeneratorRegistry) <= 128,
              "GeneratorRegistry must stay a small fixed POD");

}  // namespace sequencer
