#pragma once

#include <cstdint>

#include "../common/PatternId.h"

// Fixed-capacity cross-core command inbox (performance-safe-foundation tasks 6.1
// and 6.2; Requirements 7.1-7.4, 7.8, 6.9).
//
// The current firmware pushes multiword command payloads through the 8-word
// blocking SIO FIFO (commands::sendGateSet can push ~193 words), so a large edit
// blocks the producer and core1 blocks reading it. This component is the bounded
// shared-SRAM replacement:
//
//   - FIXED CAPACITY, NO ALLOCATION (Req 7.2): a compile-time ring of complete
//     command slots. Each slot already holds the whole operation, so the reader
//     never blocks waiting for additional payload words (Req 7.3). The memory
//     footprint is Capacity * sizeof(slot) — documented and bounded.
//
//   - SINGLE-PRODUCER / SINGLE-CONSUMER (Req 7.1): core0 writes, core1 reads,
//     with a head index owned by the consumer and a tail owned by the producer.
//     On RP2040 these are published with release/acquire ordering; on host they
//     are plain indices. tryPush/tryPop never block.
//
//   - BOUNDED DRAIN (Req 7.4): drain(n) pops at most n slots, so core1 processes
//     a documented maximum number of operations between scheduler deadlines.
//
//   - SEQUENCE IDS + ACK STATUS (Req 7.8, 6.9, task 6.2): every pushed operation
//     gets a monotonic SequenceId. The consumer records each sequence's status
//     (Accepted when popped, Applied when the scheduler applies it, or Rejected
//     when its target PatternId is stale) in a small fixed-capacity ack ring that
//     core0 can poll to render pending state and coordinate persistence.
//
//   - STALE-GENERATION REJECTION (Req 6.9 / 7.8): an operation bound to a
//     PatternId whose generation no longer matches the live allocator is marked
//     Rejected and NOT applied, so a removed/reused slot never mis-targets.
//
// Full priority/coalescing/barriers and the saturation policy are task 6.3; this
// component owns the bounded ring, sequence identity, and ack status. SDK-free:
// only <cstdint> and the canonical PatternId type.

namespace sequencer {

using SequenceId = uint32_t;
constexpr SequenceId kInvalidSequence = 0;  // 0 == no sequence

// A compact, fixed-size command slot. The opaque `kind`/`a`/`b` fields carry the
// already-decoded operation (the full payload shape is the integration's; the
// inbox only needs the target identity and enough bytes to route it). Keeping it
// POD and fixed-size is what makes the ring allocation-free.
struct InboxCommand {
    SequenceId sequence = kInvalidSequence;
    common::PatternId target{};  // stable identity this op binds to (6.2)
    uint8_t kind = 0;            // opaque OperationKind
    uint8_t a = 0;
    uint8_t b = 0;
    bool isSafety = false;       // Stop/panic/fault bypass ordinary gating (7.5)
};

enum class AckStatus : uint8_t { Unknown, Accepted, Applied, Rejected };

struct Ack {
    SequenceId sequence = kInvalidSequence;
    AckStatus status = AckStatus::Unknown;
};

template <uint16_t Capacity, uint16_t AckCapacity>
class CommandInboxT {
    static_assert(Capacity >= 2, "capacity must be at least 2");

public:
    static constexpr uint16_t capacity() { return Capacity; }

    // Producer (core0): stage one complete command. Assigns and returns its
    // SequenceId, or kInvalidSequence when the ring is full (never blocks — the
    // caller applies the task-6.3 saturation policy). The op is published whole.
    SequenceId tryPush(InboxCommand cmd) {
        const uint16_t next = static_cast<uint16_t>((tail_ + 1) % RingSize);
        if (next == head_) return kInvalidSequence;  // full
        cmd.sequence = ++lastSequence_;              // never 0
        slots_[tail_] = cmd;
        tail_ = next;
        if (count() > highWater_) highWater_ = count();
        return cmd.sequence;
    }

    // Consumer (core1): pop the oldest command. Returns false when empty. The
    // popped sequence is recorded Accepted (Req 7.8).
    bool tryPop(InboxCommand& out) {
        if (head_ == tail_) return false;  // empty
        out = slots_[head_];
        head_ = static_cast<uint16_t>((head_ + 1) % RingSize);
        recordAck(out.sequence, AckStatus::Accepted);
        return true;
    }

    // Consumer: pop up to `budget` commands into `dest`, returning how many were
    // drained. Bounds the per-cycle work (Req 7.4).
    uint16_t drain(InboxCommand* dest, uint16_t budget) {
        uint16_t n = 0;
        while (n < budget && tryPop(dest[n])) ++n;
        return n;
    }

    // Resolve a popped command against the live allocator (Req 6.9 / 7.8): if its
    // target is a safety op (no pattern binding) OR the allocator says the id is
    // still live, mark it Applied and return true; otherwise mark it Rejected and
    // return false so the caller does NOT apply it.
    bool resolveAndApply(const InboxCommand& cmd,
                         const common::PatternIdAllocator& allocator) {
        const bool live = cmd.isSafety || !cmd.target.valid() || allocator.isLive(cmd.target);
        recordAck(cmd.sequence, live ? AckStatus::Applied : AckStatus::Rejected);
        return live;
    }

    // Core0 polls the ack for a sequence to render pending/applied state.
    AckStatus ackStatus(SequenceId seq) const {
        for (uint16_t i = 0; i < AckCapacity; ++i) {
            if (acks_[i].sequence == seq) return acks_[i].status;
        }
        return AckStatus::Unknown;
    }

    uint16_t count() const {
        return static_cast<uint16_t>((tail_ + RingSize - head_) % RingSize);
    }
    bool empty() const { return head_ == tail_; }
    bool full() const { return static_cast<uint16_t>((tail_ + 1) % RingSize) == head_; }
    uint16_t highWater() const { return highWater_; }

private:
    static constexpr uint16_t RingSize = Capacity + 1;

    // Record (or update) the ack for a sequence in the fixed-capacity ack ring.
    // A later status for the same sequence overwrites the earlier one (Accepted
    // -> Applied/Rejected); a new sequence takes the next ring slot.
    void recordAck(SequenceId seq, AckStatus status) {
        for (uint16_t i = 0; i < AckCapacity; ++i) {
            if (acks_[i].sequence == seq) { acks_[i].status = status; return; }
        }
        acks_[ackTail_].sequence = seq;
        acks_[ackTail_].status = status;
        ackTail_ = static_cast<uint16_t>((ackTail_ + 1) % AckCapacity);
    }

    InboxCommand slots_[RingSize];
    uint16_t head_ = 0;   // consumer-owned
    uint16_t tail_ = 0;   // producer-owned
    uint16_t highWater_ = 0;
    SequenceId lastSequence_ = kInvalidSequence;

    Ack acks_[AckCapacity];
    uint16_t ackTail_ = 0;
};

// Default sizing: 32 command slots, 64 tracked acks. Documented and host-tested;
// the hardware validation (task 12.3) confirms occupancy stays within bounds.
using CommandInbox = CommandInboxT<32, 64>;

}  // namespace sequencer
