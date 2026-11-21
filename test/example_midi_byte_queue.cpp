// Instruments tasks 14.4 / 15.3 -- non-blocking MIDI output buffer and the overload fault.
//
//   - channel bytes keep their order; real-time bytes overtake buffered channel bytes;
//   - an empty UART FIFO gets at most kBurst bytes per pump, a busy one none;
//   - a full ring waits (pumps) instead of dropping a byte;
//   - the late timestamp is set once 10 ms of bytes are queued;
//   - a Sequencer fan-out burst through the queue reaches the wire complete and balanced, and a
//     burst that drains within a beat does not fault;
//   - a backlog that never empties for a whole beat latches MIDI_OUTPUT_OVERLOAD: transport
//     stops, notes are released, Stop is sent; Play clears the fault.
// Own main(), excluded from the glob.

#include "FakeMonotonicClock.h"
#include "MidiByteQueue.h"
#include "command.h"
#include "midi_messages.h"
#include "sequencer.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using sequencer::QueuedMidiOutput;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// UART double: `empty` says whether the transmit FIFO is empty; put() appends to the wire and
// (with autoDrain) the FIFO empties again at once, like a very fast cable.
struct FakeSink {
    bool empty = true;
    bool autoDrain = true;
    std::vector<uint8_t> wire;
    uint32_t putsSinceCheck = 0;
    bool txEmpty() {
        putsSinceCheck = 0;
        return empty;
    }
    void put(uint8_t b) {
        wire.push_back(b);
        ++putsSinceCheck;
        if (!autoDrain) empty = false;
    }
};

void test_order_and_realtime_priority() {
    FakeSink sink;
    sink.autoDrain = false;
    QueuedMidiOutput<FakeSink> q(sink);
    const uint8_t msg[] = {0x90, 60, 100, 0x91, 62, 90, 0x80, 60, 0};
    for (uint8_t b : msg) q.write(b);
    q.write(0xF8);  // clock produced after the notes
    check(q.backlog() == 10, "ten bytes buffered");

    q.pump(0);  // FIFO empty: kBurst bytes, real-time first
    check(sink.wire.size() == QueuedMidiOutput<FakeSink>::kBurst, "one burst of kBurst bytes");
    check(sink.wire[0] == 0xF8, "real-time byte overtakes the buffered notes");
    check(sink.wire[1] == 0x90 && sink.wire[2] == 60 && sink.wire[3] == 100, "then channel bytes in order");

    q.pump(1);  // FIFO not empty: nothing moves
    check(sink.wire.size() == 4, "a busy FIFO gets nothing");

    while (q.backlog() > 0) { sink.empty = true; q.pump(2); }
    const std::vector<uint8_t> expect = {0xF8, 0x90, 60, 100, 0x91, 62, 90, 0x80, 60, 0};
    check(sink.wire == expect, "complete stream, channel order kept");
}

void test_full_ring_waits_and_drops_nothing() {
    FakeSink sink;  // autoDrain: every pump can send a burst
    QueuedMidiOutput<FakeSink> q(sink);
    const uint32_t n = QueuedMidiOutput<FakeSink>::kCapacity + 100;
    for (uint32_t i = 0; i < n; ++i) q.write(static_cast<uint8_t>(i & 0x7F));
    check(q.fullWaits() > 0, "writing past capacity waited for the wire");
    while (q.backlog() > 0) q.pump(0);
    bool same = sink.wire.size() == n;
    for (uint32_t i = 0; same && i < n; ++i) same = sink.wire[i] == static_cast<uint8_t>(i & 0x7F);
    check(same, "every byte reached the wire, in order");
}

void test_late_timestamp() {
    FakeSink sink;
    sink.empty = false;  // nothing drains
    QueuedMidiOutput<FakeSink> q(sink);
    for (int i = 0; i < 30; ++i) q.write(0x40);  // 30 x 320 us = 9.6 ms
    q.pump(5000000);
    check(q.lastLateMs() == 0, "9.6 ms of queued bytes is not late");
    q.write(0x40);                               // 31 x 320 us = 9.92 ms
    q.write(0x40);                               // 32 x 320 us = 10.24 ms
    q.pump(6000000);
    check(q.lastLateMs() == 6000, "10 ms of queued bytes records the time (ms)");
}

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m;
    m.cmd = c;
    m.param1 = p1;
    return m;
}

struct Rig {
    FakeMonotonicClock clock{1000};
    FakeSink sink;
    QueuedMidiOutput<FakeSink> q{sink};
    sequencer::Sequencer seq{q, clock};
    uint64_t base = 0;

    void gate(uint8_t p, const std::vector<bool>& g) {
        auto m = cmd(commands::Command::PATTERN_GATE_SET, p);
        m.gateCount = static_cast<uint16_t>(g.size());
        m.gates = g;
        seq.processCommand(m);
    }
    void pitch(uint8_t p, uint8_t note) {
        auto m = cmd(commands::Command::PATTERN_PITCH_SET, p);
        m.pitchCount = 1;
        m.pitches = {note};
        seq.processCommand(m);
    }
    void play() {
        base = clock.nowMicros();
        seq.processCommand(cmd(commands::Command::PLAY));
    }
    // One core1 loop at tick n: update, then pump like sequencer_task.
    void tick(uint64_t n) {
        clock.setMicros(base + (n * 60ULL * 1000000ULL) / (120ULL * 24ULL));
        q.pump(clock.nowMicros());
        seq.update();
        q.pump(clock.nowMicros());
    }
};

// Wire model: at 120 BPM one tick (20.8 ms) carries 65 bytes, about 16 bursts of kBurst bytes.
void wireTick(Rig& r, uint64_t n) {
    r.sink.autoDrain = false;
    // Like the core1 loop: update and pump alternate many times within one tick.
    const uint64_t start = r.base + (n * 60ULL * 1000000ULL) / (120ULL * 24ULL);
    for (int i = 0; i < 16; ++i) {
        r.clock.setMicros(start + static_cast<uint64_t>(i) * 1280u);
        r.seq.update();
        r.sink.empty = true;
        r.q.pump(r.clock.nowMicros());
    }
}

// Six patterns playing every sixteenth on `routes` Instruments. Returns the wire stream.
std::vector<uint8_t> fanOut(Rig& r, uint8_t routes, uint64_t ticks) {
    for (int i = 1; i < 6; ++i) r.seq.processCommand(cmd(commands::Command::PATTERN_ADD));
    for (uint8_t p = 0; p < 6; ++p) {
        r.gate(p, {true, true, true, false, false, false});
        r.pitch(p, static_cast<uint8_t>(60 + p));
    }
    for (uint8_t slot = 0; slot < routes; ++slot) r.seq.applyInstrument(slot, slot, 0b111111);
    r.play();
    for (uint64_t n = 1; n <= ticks; ++n) wireTick(r, n);
    r.seq.processCommand(cmd(commands::Command::STOP));
    r.sink.autoDrain = true;
    r.sink.empty = true;
    while (r.q.backlog() > 0) r.q.pump(r.clock.nowMicros());
    return r.sink.wire;
}

// Note On count, On/Off violations and held keys; real-time bytes may sit inside a message.
void analyse(const std::vector<uint8_t>& raw, int& notes, int& violations, int& held) {
    std::vector<uint8_t> w;
    for (uint8_t b : raw) if (!sequencer::isMidiRealTime(b)) w.push_back(b);
    bool on[16][128] = {{false}};
    notes = violations = held = 0;
    for (size_t i = 0; i + 2 < w.size() + 1 && i < w.size();) {
        const uint8_t st = w[i] & 0xF0;
        if ((st == 0x90 || st == 0x80) && i + 2 < w.size()) {
            const uint8_t ch = w[i] & 0x0F, note = w[i + 1] & 0x7F;
            const bool isOn = st == 0x90 && w[i + 2] > 0;
            if (isOn == on[ch][note]) ++violations;
            on[ch][note] = isOn;
            if (isOn) ++notes;
            i += 3;
        } else {
            ++i;
        }
    }
    for (auto& row : on) for (bool b : row) held += b ? 1 : 0;
}

// 8 Instruments: 48 Note Ons in one tick (144 bytes, more than two ticks of wire time), but the
// average fits the cable. The burst drains, nothing faults, nothing is lost or left hanging.
void test_fan_out_burst_through_the_queue() {
    Rig r;
    const std::vector<uint8_t> w = fanOut(r, 8, 96);
    int notes, violations, held;
    analyse(w, notes, violations, held);
    std::printf("  8 routes: notes=%d violations=%d held=%d fault=%d missed=%llu peak=%u bytes=%zu\n", notes,
                violations, held, r.seq.timingFaultLatched() ? 1 : 0,
                static_cast<unsigned long long>(r.seq.performanceMetrics().missedLogicalTicks),
                r.q.peakBacklog(), w.size());
    check(notes == 16 * 6 * 8, "every note of every route reached the wire");
    check(violations == 0 && held == 0, "On/Off alternate per key and nothing is held after Stop");
    check(!r.seq.timingFaultLatched(), "a burst that drains does not fault");
    check(r.seq.performanceMetrics().missedLogicalTicks == 0, "no tick was skipped");
    int clocks = 0;
    for (uint8_t b : w) clocks += b == 0xF8 ? 1 : 0;
    check(clocks == 96, "every MIDI Clock byte was sent");
}

// 16 Instruments: twice what the cable carries on average. The fault latches and the stream
// still ends balanced.
void test_average_overload_latches_and_stays_balanced() {
    Rig r;
    const std::vector<uint8_t> w = fanOut(r, 16, 96);
    int notes, violations, held;
    analyse(w, notes, violations, held);
    check(r.seq.timingFaultLatched(), "sustained overload latches the fault");
    check(violations == 0 && held == 0, "balanced after the fault");
}

// A slow cable (one burst of kBurst bytes per tick) under a 16-route load: the backlog never
// empties for a whole beat and the overload fault latches.
void test_sustained_backlog_latches_overload() {
    Rig r;
    r.gate(0, {true, false});
    r.pitch(0, 60);
    for (uint8_t slot = 0; slot < 16; ++slot) r.seq.applyInstrument(slot, slot, 0b1);
    r.play();
    r.sink.autoDrain = false;
    auto slowTick = [&](uint64_t n) { r.sink.empty = true; r.tick(n); };
    // The buffer grows each tick; the fault must latch exactly when it first holds 250 ms of
    // bytes (782), never before.
    bool early = false;
    uint64_t n = 1;
    for (; n <= 200 && !r.seq.timingFaultLatched(); ++n) {
        slowTick(n);
        if (r.seq.timingFaultLatched() && r.q.peakBacklog() < sequencer::kMidiOverloadBacklogBytes) early = true;
        if (!r.seq.timingFaultLatched() && r.q.backlog() >= sequencer::kMidiOverloadBacklogBytes + 100) early = true;
    }
    check(!early, "the fault latches at 782 buffered bytes (250 ms), not before");
    check(r.seq.timingFaultLatched(), "fault once 250 ms of bytes are waiting");
    check(r.seq.timingFaultCause() == sequencer::PerformanceFaultCause::MIDI_OUTPUT_OVERLOAD,
          "cause is MIDI output overload");

    // Drain: the release and the Stop follow the buffered bytes.
    r.sink.autoDrain = true;
    r.sink.empty = true;
    while (r.q.backlog() > 0) r.q.pump(r.clock.nowMicros());
    const std::vector<uint8_t>& w = r.sink.wire;
    bool sawStop = false;
    for (uint8_t b : w) sawStop |= b == sequencer::midi::SystemRealTimeMessage::STOP;
    check(sawStop, "MIDI Stop sent");
    int notes, violations, held;
    analyse(w, notes, violations, held);
    check(notes > 0 && violations == 0 && held == 0, "every Note On got its Note Off");

    r.seq.processCommand(cmd(commands::Command::PLAY));
    check(!r.seq.timingFaultLatched(), "Play acknowledges the fault");
}

}  // namespace

int main() {
    test_order_and_realtime_priority();
    test_full_ring_waits_and_drops_nothing();
    test_late_timestamp();
    test_fan_out_burst_through_the_queue();
    test_average_overload_latches_and_stays_balanced();
    test_sustained_backlog_latches_overload();
    if (failures == 0) std::printf("example_midi_byte_queue: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
