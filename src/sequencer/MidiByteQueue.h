#pragma once

#include <cstdint>

#include "IMidiOutput.h"

// Non-blocking MIDI byte output (help-me-brainstorm-an-additional, Requirement 11, design
// section 11).
//
// The sequencer produces every byte of a tick at once, but the DIN wire carries one byte per
// 320 us. With a blocking UART write a burst larger than the 32-byte UART FIFO made core1 wait
// on the cable, and a burst longer than one tick made the scheduler skip ticks (their Note Ons
// and Clock bytes were dropped). QueuedMidiOutput instead buffers the bytes and lets core1's
// main loop hand them to the UART as it accepts them (pump()).
//
//   - Channel bytes go through a fixed 4096-byte ring in FIFO order.
//   - Real-time bytes (Clock, Start, Continue, Stop) use a small separate lane that is drained
//     first. MIDI allows a real-time byte between the bytes of any other message, so this is
//     legal on the wire and keeps the clock on time under load.
//   - pump() only refills the UART when its FIFO is empty, and then with at most kBurst bytes, so
//     a real-time byte waits behind at most kBurst bytes (1.28 ms).
//   - A full ring never drops a byte: write() pumps until there is space (the previous blocking
//     behaviour, now only reached under extreme load).
//
// SDK-free: the UART is reached through the Sink template parameter
//   struct Sink { bool txEmpty(); void put(uint8_t byte); };
// so the queue runs unchanged in host tests with a fake sink.

namespace sequencer {

constexpr uint32_t kMidiByteMicros = 320;          // 10 bits at 31250 baud
constexpr uint32_t kMidiLateThresholdUs = 10000;   // load indicator: a byte waited >= 10 ms

constexpr bool isMidiRealTime(uint8_t b) {
    return b == 0xF8 || b == 0xFA || b == 0xFB || b == 0xFC;
}

template <typename Sink>
class QueuedMidiOutput : public IMidiOutput {
public:
    static constexpr uint32_t kCapacity = 4096;   // channel-byte ring
    static constexpr uint32_t kRealTimeCapacity = 8;
    static constexpr uint32_t kBurst = 4;         // bytes handed to an empty UART FIFO per pump

    explicit QueuedMidiOutput(Sink& sink) : sink_(sink) {}

    void write(uint8_t byte) override {
        if (isMidiRealTime(byte)) {
            while (rtCount_ >= kRealTimeCapacity) pump(nowUs_);
            rt_[(rtHead_ + rtCount_) % kRealTimeCapacity] = byte;
            ++rtCount_;
            return;
        }
        // Full ring: wait for the wire rather than drop a byte (Requirement 11.4).
        while (count_ >= kCapacity) {
            ++fullWaits_;
            pump(nowUs_);
        }
        ring_[(head_ + count_) % kCapacity] = byte;
        ++count_;
        if (count_ > peak_) peak_ = count_;
    }

    // Move buffered bytes to the UART. Call from core1's main loop as often as possible.
    void pump(uint64_t nowUs) {
        nowUs_ = nowUs;
        if (sink_.txEmpty()) {
            for (uint32_t i = 0; i < kBurst; ++i) {
                if (rtCount_ > 0) {
                    sink_.put(rt_[rtHead_]);
                    rtHead_ = (rtHead_ + 1) % kRealTimeCapacity;
                    --rtCount_;
                } else if (count_ > 0) {
                    sink_.put(ring_[head_]);
                    head_ = (head_ + 1) % kCapacity;
                    --count_;
                } else {
                    break;
                }
            }
        }
        // Queued delay of the newest byte: everything ahead of it must cross the wire first.
        if (static_cast<uint64_t>(count_) * kMidiByteMicros >= kMidiLateThresholdUs) {
            const uint32_t ms = static_cast<uint32_t>(nowUs / 1000u);
            lastLateMs_ = ms == 0 ? 1 : ms;  // 0 means "never"
        }
    }

    uint32_t backlog() const override { return count_ + rtCount_; }

    // Time (ms since boot) a byte last waited >= kMidiLateThresholdUs; 0 = never.
    uint32_t lastLateMs() const { return lastLateMs_; }
    uint32_t peakBacklog() const { return peak_; }
    uint32_t fullWaits() const { return fullWaits_; }
    void resetPeak() { peak_ = count_; fullWaits_ = 0; }

private:
    Sink& sink_;
    uint8_t ring_[kCapacity] = {0};
    uint32_t head_ = 0;
    uint32_t count_ = 0;
    uint8_t rt_[kRealTimeCapacity] = {0};
    uint32_t rtHead_ = 0;
    uint32_t rtCount_ = 0;
    uint64_t nowUs_ = 0;
    uint32_t lastLateMs_ = 0;
    uint32_t peak_ = 0;
    uint32_t fullWaits_ = 0;
};

}  // namespace sequencer
