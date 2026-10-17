#pragma once

#include "IMonotonicClock.h"

#include "pico/time.h"

namespace sequencer {

/// Firmware monotonic clock backed by the Pico SDK absolute boot timer.
class PicoMonotonicClock final : public IMonotonicClock {
public:
    uint64_t nowMicros() const override {
        return static_cast<uint64_t>(to_us_since_boot(get_absolute_time()));
    }
};

}  // namespace sequencer
