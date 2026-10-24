#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

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
//   - GENERATIVE COMMAND PASSENGERS (generative-engine-expansion task 14,
//     FR5/FR9/FR10): the Mutate / Undo / Revert / Keep commands ride the same
//     bounded ring as fixed-size POD passengers (below). The sequencer's
//     consumer drains them with a bounded budget, resolves each one's
//     application tick through the three-level timing policy, and publishes
//     the final Applied / Rejected status over the same ack ring via markAck.
//
// Full priority/coalescing/barriers and the saturation policy are task 6.3; this
// component owns the bounded ring, sequence identity, and ack status. SDK-free:
// only <cstdint> / <cstring> / <type_traits> and the canonical PatternId type.

namespace sequencer {

using SequenceId = uint32_t;
constexpr SequenceId kInvalidSequence = 0;  // 0 == no sequence

// ---------------------------------------------------------------------------
// Generative command passengers (task 14, FR5/FR9/FR10)
//
// The four recoverable generative-layer commands the sequencer dispatches
// (FR5). They ride the SAME bounded ring as every other operation, as fixed-
// size POD passengers — no new queue, no allocation, no blocking. The
// sequencer resolves each one's application boundary through the three-level
// timing policy (level-1 per-invocation override -> level-2 per-operation
// mode -> level-3 global default, TimingPolicy.h) and executes it against the
// working state (WorkingStateManager + the mutation operator registry).
// ---------------------------------------------------------------------------

// Sentinel pattern index for "every live pattern" (FR5's all-patterns
// Mutate): one command fans out over the live pattern slots in order, each
// application pushing its own undo snapshot.
constexpr uint8_t kAllPatterns = 0xFF;

// Fixed storage for one mutation operator id (bytes incl. the NUL). Mirrors
// PatternGenerativeState's kGeneratorIdCapacity so every registered operator
// id is addressable; kept local so this header stays self-contained.
constexpr uint8_t kMutationOpIdCapacity = 24;

// Level-1 timing override for ONE generative command invocation (FR10).
// `mode` carries the OperationTimingMode ordinal (OperationTimingConfig.h):
// 0 Immediate, 1 QuantizeBeat, 2 QuantizeBar — a raw byte because this
// header is SDK-free and only the dispatcher interprets it. `active == false`
// means no override was supplied and resolution falls to levels 2 and 3.
struct GenerativeTimingOverride {
    bool active = false;
    uint8_t mode = 0;  // OperationTimingMode ordinal: 0/1/2
};

// The Mutate command (FR5/FR6/FR7/FR8): WHICH registered operator to run,
// on which pattern (a slot index or every live pattern), with the FR7 target
// mask, the FR6 intensity, the FR8 invariants and distance limit the
// operator must respect. All fixed-size POD — trivially snapshotted and
// crossed the ring whole.
struct MutateCmd {
    char opId[kMutationOpIdCapacity] = {};
    uint8_t patternIndex = kAllPatterns;
    uint16_t targets = 0;     // MutationTargetMask (FR7)
    uint8_t intensity = 0;     // 0-127 (FR6); 0 is a deliberate no-change
    uint16_t invariants = 0;  // MutationInvariantMask (FR8)
    uint8_t maxDistance = 255;  // FR8 distance limit (kUnlimitedMutationDistance)
};

// The three recovery commands (FR9/FR15). They carry no payload of their
// own beyond the shared timing override: Undo pops the global undo stack,
// Revert restores every pattern's baseline and clears the stack, Keep
// promotes the working state into the baseline (WorkingStateManager, T12/
// T13). The named variants exist so the dispatch's `kind` discriminated
// union stays closed and explicit.
struct UndoCmd {};
struct RevertCmd {};
struct KeepCmd {};

// Which generative command a slot carries.
enum class GenerativeCommandKind : uint8_t {
    None = 0,  // an ordinary (non-generative) inbox operation
    Mutate = 1,
    Undo = 2,
    Revert = 3,
    Keep = 4,
};

// One complete generative command: the kind, the level-1 timing override
// and (for Mutate) the operator request. The recovery commands leave
// `mutate` at its defaults.
struct GenerativeCommand {
    GenerativeCommandKind kind = GenerativeCommandKind::None;
    GenerativeTimingOverride timing{};
    MutateCmd mutate{};
};

static_assert(std::is_trivially_copyable<GenerativeCommand>::value,
              "generative command passengers must stay POD");

// Copy `id` into the fixed operator-id storage. An id that does NOT fit is
// truncated to an EMPTY id — dispatch then rejects the command (a too-long
// id never silently aliases a different operator).
inline void copyMutationOpId(char (&dst)[kMutationOpIdCapacity], const char* id) {
    dst[0] = '\0';
    if (id == nullptr) return;
    const size_t n = std::strlen(id);
    if (n >= kMutationOpIdCapacity) return;
    std::memcpy(dst, id, n + 1);
}

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
    // Coalescing key (task 6.3): a replaceable per-pattern property (e.g. gate /
    // pitch / velocity). A later edit to the SAME (target, property) may replace
    // an earlier pending one latest-wins — but only when neither is a barrier and
    // no barrier sits between them. 0 == not coalescible (always queued).
    uint8_t property = 0;
    // Ordering barrier (task 6.3): transport transitions, Pattern removal, and
    // save snapshots must NOT be coalesced across — edits on either side keep
    // their relative order. A barrier op is itself never coalesced.
    bool isBarrier = false;
    // Generative passenger (task 14): the Mutate / Undo / Revert / Keep
    // command this slot carries, or kind None for an ordinary operation.
    // A fixed POD passenger keeps the whole slot allocation-free; the added
    // footprint (one GenerativeCommand per slot, Capacity * ~40 bytes) is the
    // documented cost of routing the generative layer over the same bounded
    // ring. Generative commands are never coalesced (property stays 0): each
    // application is its own undo step.
    GenerativeCommand gen;
};

static_assert(std::is_trivially_copyable<InboxCommand>::value,
              "the command slot must stay POD (fixed-size ring storage)");

// Factory: one Mutate command. `timing` is the optional level-1 override
// (FR10); without it the operation's configured mode applies (level 2).
inline InboxCommand makeMutateCommand(const char* opId, uint8_t patternIndex,
                                      uint16_t targets, uint8_t intensity,
                                      uint16_t invariants = 0,
                                      uint8_t maxDistance = 255,
                                      GenerativeTimingOverride timing = {}) {
    InboxCommand cmd{};
    cmd.gen.kind = GenerativeCommandKind::Mutate;
    cmd.gen.timing = timing;
    copyMutationOpId(cmd.gen.mutate.opId, opId);
    cmd.gen.mutate.patternIndex = patternIndex;
    cmd.gen.mutate.targets = targets;
    cmd.gen.mutate.intensity = intensity;
    cmd.gen.mutate.invariants = invariants;
    cmd.gen.mutate.maxDistance = maxDistance;
    return cmd;
}

// Factories: the three recovery commands, each with an optional level-1
// timing override (they default to Immediate through their level-2 modes).
inline InboxCommand makeUndoCommand(GenerativeTimingOverride timing = {}) {
    InboxCommand cmd{};
    cmd.gen.kind = GenerativeCommandKind::Undo;
    cmd.gen.timing = timing;
    return cmd;
}

inline InboxCommand makeRevertCommand(GenerativeTimingOverride timing = {}) {
    InboxCommand cmd{};
    cmd.gen.kind = GenerativeCommandKind::Revert;
    cmd.gen.timing = timing;
    return cmd;
}

inline InboxCommand makeKeepCommand(GenerativeTimingOverride timing = {}) {
    InboxCommand cmd{};
    cmd.gen.kind = GenerativeCommandKind::Keep;
    cmd.gen.timing = timing;
    return cmd;
}

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

    // Producer: push with latest-wins COALESCING (task 6.3, Req 7.6/7.7). If a
    // pending op targets the same (target, property) and is coalescible (both
    // non-barrier, non-safety) AND no BARRIER sits between it and the tail, the
    // pending op's payload is REPLACED in place and its (already-assigned)
    // sequence is returned — no new slot is consumed. Otherwise this behaves like
    // tryPush. Coalescing never crosses a transport/remove/snapshot barrier
    // (Req 7.7), so edits on opposite sides of one keep their order.
    SequenceId tryPushCoalescing(InboxCommand cmd) {
        if (cmd.property != 0 && !cmd.isBarrier && !cmd.isSafety) {
            // Scan pending slots newest-first; stop at the first barrier so we
            // never coalesce across it.
            uint16_t k = tail_;
            while (k != head_) {
                k = static_cast<uint16_t>((k + RingSize - 1) % RingSize);
                InboxCommand& pending = slots_[k];
                if (pending.isBarrier) break;  // do not coalesce across a barrier
                if (!pending.isSafety && pending.property == cmd.property &&
                    pending.target == cmd.target) {
                    // Latest-wins: replace payload, keep the existing sequence.
                    const SequenceId keep = pending.sequence;
                    cmd.sequence = keep;
                    pending = cmd;
                    return keep;
                }
            }
        }
        return tryPush(cmd);
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

    // Consumer: pop the highest-PRIORITY pending command (task 6.3, Req 7.5).
    // A pending SAFETY op (Stop/panic/fault) is returned ahead of any ordinary
    // edit, regardless of FIFO position; among same-priority ops, FIFO order is
    // preserved. Returns false when empty. The popped sequence is Accepted.
    bool popPriority(InboxCommand& out) {
        if (head_ == tail_) return false;
        // Find the oldest pending safety op, if any.
        uint16_t safetyRing = RingSize;  // sentinel
        for (uint16_t k = head_; k != tail_; k = static_cast<uint16_t>((k + 1) % RingSize)) {
            if (slots_[k].isSafety) { safetyRing = k; break; }
        }
        if (safetyRing == RingSize) return tryPop(out);  // no safety op -> FIFO
        out = slots_[safetyRing];
        removeAt(safetyRing);
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

    // Consumer (task 14): publish the FINAL status of a popped generative
    // command once it has been executed (or dropped). Overwrites the
    // Accepted recorded at pop time, exactly like resolveAndApply does for
    // transport commands; a later status always wins.
    void markAck(SequenceId seq, AckStatus status) { recordAck(seq, status); }

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

    // Remove the slot at ring position `ring` by shifting the entries after it
    // (in logical order) one position toward head, then retracting the tail.
    // Keeps the ring contiguous between head_ and tail_ so popPriority can take
    // a safety op out of FIFO order. Bounded by count().
    void removeAt(uint16_t ring) {
        uint16_t cur = ring;
        while (true) {
            const uint16_t nxt = static_cast<uint16_t>((cur + 1) % RingSize);
            if (nxt == tail_) break;
            slots_[cur] = slots_[nxt];
            cur = nxt;
        }
        tail_ = static_cast<uint16_t>((tail_ + RingSize - 1) % RingSize);
    }

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
