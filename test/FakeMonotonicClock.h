#pragma once

#include "sequencer/IMonotonicClock.h"

#include <cstdint>

/// Controllable host clock for deterministic production-scheduler tests.
class FakeMonotonicClock final : public sequencer::IMonotonicClock {
public:
    explicit FakeMonotonicClock(uint64_t initialMicros = 0)
        : nowMicros_(initialMicros) {}

    uint64_t nowMicros() const override { return nowMicros_; }

    void setMicros(uint64_t value) { nowMicros_ = value; }

    void advanceMicros(uint64_t delta) { nowMicros_ += delta; }

private:
    uint64_t nowMicros_;
};
