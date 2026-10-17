#pragma once

#include <cstdint>

namespace sequencer {

/// Monotonic time source used by the sequencer scheduler.
///
/// Time is expressed as unsigned 64-bit microseconds. Implementations must not
/// move backwards during the lifetime of a Sequencer.
class IMonotonicClock {
public:
    virtual ~IMonotonicClock() = default;

    virtual uint64_t nowMicros() const = 0;
};

}  // namespace sequencer
