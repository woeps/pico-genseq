#pragma once

#include <algorithm>
#include <cstdint>

namespace sequencer {

enum class PerformanceFaultCause : uint8_t {
    NONE,
    SCHEDULER_LATENESS,
    MIDI_OUTPUT_OVERLOAD,
    COMMAND_PROTOCOL,
    NOTE_OWNERSHIP,
    STORAGE,
};

/// Fixed-size development metrics for the timing-critical sequencer path.
///
/// Queue, reconciliation, and fault fields are present before their owning
/// Milestone 2 components exist so later work can extend instrumentation without
/// changing the public metrics shape. They remain zero/NONE until those
/// components report observations.
struct PerformanceMetrics {
    uint64_t logicalTicksServiced = 0;
    uint64_t maxSchedulerLatenessUs = 0;
    uint64_t missedLogicalTicks = 0;
    uint64_t reconciliationCount = 0;
    uint64_t midiBytesDispatched = 0;
    uint64_t operationsApplied = 0;
    uint32_t midiQueueHighWater = 0;
    uint32_t commandQueueHighWater = 0;
    PerformanceFaultCause lastFault = PerformanceFaultCause::NONE;

    void observeSchedulerTick(uint64_t latenessUs, uint64_t missedTickCount) {
        ++logicalTicksServiced;
        maxSchedulerLatenessUs = std::max(maxSchedulerLatenessUs, latenessUs);
        missedLogicalTicks += missedTickCount;
    }

    void observeMidiQueueDepth(uint32_t depth) {
        midiQueueHighWater = std::max(midiQueueHighWater, depth);
    }

    void observeCommandQueueDepth(uint32_t depth) {
        commandQueueHighWater = std::max(commandQueueHighWater, depth);
    }

    void recordReconciliation() { ++reconciliationCount; }

    void recordFault(PerformanceFaultCause cause) { lastFault = cause; }
};

}  // namespace sequencer
