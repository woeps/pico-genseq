// Instruments tasks 5.3 / 4.3 -- INSTRUMENT_SET wire format and core1 fan-out.
//
// Runs the REAL command.cpp encoder/decoder over a std::queue FIFO double and the real Sequencer:
//   - INSTRUMENT_SET round-trips (slot, channel, mask) through both receive paths and drains the
//     FIFO exactly (header + one mask word);
//   - one pattern routed to three Instruments sends the SAME note on three channels in ascending
//     slot order, and releases each on its own channel;
//   - removing a route, muting (mask 0) and changing a channel each emit the Note Off on the
//     channel recorded at Note On and leave the other routes sounding;
//   - malformed commands (slot, channel or mask out of range) change nothing;
//   - a seeded, probabilistic pattern produces the same notes after a route is (re-)enabled as
//     one that was routed all along (the random draws do not depend on routing, R9.7);
//   - PATTERN_REMOVE compacts the route table like the UI does.
// Own main(), excluded from the glob.

#include <cstdint>
#include <cstdio>
#include <queue>
#include <vector>

static std::queue<uint32_t> g_fifo;

void multicore_fifo_push_blocking(uint32_t v) { g_fifo.push(v); }
uint32_t multicore_fifo_pop_blocking(void) {
    if (g_fifo.empty()) return 0;
    const uint32_t v = g_fifo.front();
    g_fifo.pop();
    return v;
}
bool multicore_fifo_rvalid(void) { return !g_fifo.empty(); }

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "command.h"
#include "PatternSnapshot.h"
#include "sequencer.h"
#include "midi_messages.h"

namespace midi = sequencer::midi;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

constexpr uint8_t ON = midi::ChannelVoiceMessage::NOTE_ON;
constexpr uint8_t OFF = midi::ChannelVoiceMessage::NOTE_OFF;

struct Core1 {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};
    sequencer::PatternSnapshot snap;
    uint64_t base = 0;

    void drain() {
        while (!g_fifo.empty()) {
            const commands::CommandMessage m = commands::receiveCommandInto(snap);
            seq.processCommand(m, snap);
        }
    }
    void addPatterns(uint8_t n) {
        for (uint8_t i = 1; i < n; ++i) commands::sendCommand(commands::Command::PATTERN_ADD);
        drain();
    }
    // Gate {on, off} (rising on odd ticks, falling on even ticks), one pitch, one velocity.
    void material(uint8_t p, uint8_t pitch, uint8_t vel = 100) {
        commands::sendGateSet(p, {true, false});
        commands::sendPitchSet(p, 1, common::PlayingOrder::FORWARDS, {pitch});
        commands::sendVelocitySet(p, 1, common::PlayingOrder::FORWARDS, {vel});
        drain();
    }
    void route(uint8_t slot, uint8_t channel, uint16_t mask) {
        commands::sendInstrumentSet(slot, channel, mask);
        drain();
    }
    void play() {
        base = clock.nowMicros();
        commands::sendCommand(commands::Command::PLAY);
        drain();
    }
    void tick(uint64_t n) {
        clock.setMicros(base + (n * 60ULL * 1000000ULL) / (120ULL * 24ULL));
        seq.update();
    }
};

std::vector<uint8_t> msg(uint8_t status, uint8_t ch, uint8_t note, uint8_t vel) {
    return {static_cast<uint8_t>(status | ch), note, vel};
}

// Channel-voice triples (status, note, velocity) from a stream; clock/start/stop bytes skipped.
std::vector<std::vector<uint8_t>> triples(const std::vector<uint8_t>& s) {
    std::vector<std::vector<uint8_t>> r;
    for (size_t i = 0; i < s.size();) {
        if ((s[i] & 0xF0) == ON || (s[i] & 0xF0) == OFF) {
            if (i + 2 < s.size() + 0) r.push_back({s[i], s[i + 1], s[i + 2]});
            i += 3;
        } else {
            ++i;
        }
    }
    return r;
}

std::vector<uint8_t> flat(const std::vector<std::vector<uint8_t>>& t) {
    std::vector<uint8_t> r;
    for (const auto& x : t) r.insert(r.end(), x.begin(), x.end());
    return r;
}

void test_wire_round_trip() {
    sequencer::PatternSnapshot snap;
    commands::sendInstrumentSet(3, 9, 0x0005);
    check(g_fifo.size() == 2, "INSTRUMENT_SET is a header word and one mask word");
    commands::CommandMessage m = commands::receiveCommandInto(snap);
    check(m.cmd == commands::Command::INSTRUMENT_SET && m.param1 == 3 && m.param2 == 9 &&
              m.patternMask == 0x0005,
          "slot, channel and mask round-trip (snapshot path)");
    check(g_fifo.empty(), "FIFO drained after the snapshot decode");

    commands::sendInstrumentSet(15, 15, 0x7FFF);
    m = commands::receiveCommand();
    check(m.cmd == commands::Command::INSTRUMENT_SET && m.param1 == 15 && m.param2 == 15 &&
              m.patternMask == 0x7FFF,
          "slot, channel and mask round-trip (heap path)");
    check(g_fifo.empty(), "FIFO drained after the heap decode");
}

void test_fan_out_order_and_release() {
    Core1 c;
    c.material(0, 60);
    c.route(1, 1, 0x0001);
    c.route(4, 5, 0x0001);
    c.play();
    c.out.clear();
    c.tick(1);
    const auto on = triples(c.out.record());
    check(on.size() == 3, "one onset sends one Note On per route");
    if (on.size() == 3) {
        check(on[0] == msg(ON, 0, 60, 100), "slot 0 (channel 1) first");
        check(on[1] == msg(ON, 1, 60, 100), "slot 1 (channel 2) second");
        check(on[2] == msg(ON, 5, 60, 100), "slot 4 (channel 6) third, ascending slot order");
    }
    c.out.clear();
    c.tick(2);
    const auto off = triples(c.out.record());
    check(off.size() == 3 && off[0] == msg(OFF, 0, 60, 0) && off[1] == msg(OFF, 1, 60, 0) &&
              off[2] == msg(OFF, 5, 60, 0),
          "each route's Note Off on its own channel, ascending slot order");
}

void test_route_edits_release_on_recorded_channel() {
    Core1 c;
    c.material(0, 60);
    c.route(1, 1, 0x0001);
    c.route(4, 5, 0x0001);
    c.play();
    c.tick(1);

    // Mute-like edit: slot 1 loses the pattern.
    c.out.clear();
    c.route(1, 1, 0x0000);
    check(flat(triples(c.out.record())) == msg(OFF, 1, 60, 0), "emptying a slot releases only its voice");

    // Channel change of slot 4: Note Off on the OLD channel 5, never on the new one.
    c.out.clear();
    c.route(4, 7, 0x0001);
    check(flat(triples(c.out.record())) == msg(OFF, 5, 60, 0),
          "channel change releases on the channel recorded at Note On");

    // Falling edge: only slot 0 still sounds.
    c.out.clear();
    c.tick(2);
    check(flat(triples(c.out.record())) == msg(OFF, 0, 60, 0), "remaining route releases normally");

    // Next onset: slot 0 on channel 0, slot 4 on its NEW channel 7, slot 1 silent.
    c.out.clear();
    c.tick(3);
    const auto on = triples(c.out.record());
    check(on.size() == 2 && on[0] == msg(ON, 0, 60, 100) && on[1] == msg(ON, 7, 60, 100),
          "next onset uses the new channel and skips the emptied slot");
}

void test_malformed_commands_change_nothing() {
    Core1 c;
    c.material(0, 60);
    c.play();
    commands::CommandMessage m;
    m.cmd = commands::Command::INSTRUMENT_SET;

    m.param1 = 16; m.param2 = 3; m.patternMask = 1;  // slot out of range
    c.seq.processCommand(m);
    m.param1 = 2; m.param2 = 16; m.patternMask = 1;  // channel out of range
    c.seq.processCommand(m);
    m.param1 = 2; m.param2 = 3; m.patternMask = 0b10;  // names a pattern that does not exist
    c.seq.processCommand(m);
    c.out.clear();
    c.tick(1);
    const auto on = triples(c.out.record());
    check(on.size() == 1 && on[0] == msg(ON, 0, 60, 100), "rejected commands left the routes unchanged");
}

// R9.7: a pattern's random draws do not depend on its routes.
std::vector<uint8_t> runSeeded(bool routedFromStart, uint64_t enableAfter, uint64_t total) {
    Core1 c;
    c.material(0, 60);
    common::GenerationParameters gp;
    gp.patternProbability = 60;
    gp.humanizeRange = 15;
    gp.seed = 1234;
    gp.seedSet = true;
    c.seq.setPatternGenerationParameters(0, gp);
    if (!routedFromStart) c.route(0, 0, 0x0000);
    c.play();
    for (uint64_t n = 1; n <= enableAfter; ++n) c.tick(n);
    if (!routedFromStart) c.route(0, 0, 0x0001);
    c.out.clear();
    for (uint64_t n = enableAfter + 1; n <= total; ++n) c.tick(n);
    return flat(triples(c.out.record()));
}

void test_draws_independent_of_routes() {
    const auto always = runSeeded(true, 40, 160);
    const auto late = runSeeded(false, 40, 160);
    check(!always.empty(), "the seeded pattern emitted notes");
    check(always == late, "notes after enabling a route equal those of an always-routed pattern");
}

void test_pattern_remove_compacts_routes() {
    Core1 c;
    c.addPatterns(3);
    c.material(0, 60);
    c.material(1, 61);
    c.material(2, 62);
    c.route(0, 0, 0b111);   // all three on channel 1
    c.route(2, 2, 0b100);   // pattern 2 also on channel 3
    c.play();
    commands::sendCommand(commands::Command::PATTERN_REMOVE, 1);
    c.drain();
    c.out.clear();
    c.tick(1);
    bool p60ch0 = false, p62ch0 = false, p62ch2 = false, p61 = false, p60ch2 = false;
    for (const auto& t : triples(c.out.record())) {
        if ((t[0] & 0xF0) != ON) continue;
        const uint8_t ch = t[0] & 0x0F;
        if (t[1] == 60 && ch == 0) p60ch0 = true;
        if (t[1] == 60 && ch == 2) p60ch2 = true;
        if (t[1] == 62 && ch == 0) p62ch0 = true;
        if (t[1] == 62 && ch == 2) p62ch2 = true;
        if (t[1] == 61) p61 = true;
    }
    check(p60ch0 && p62ch0, "patterns 0 and old 2 still on channel 1");
    check(p62ch2, "old pattern 2 (now index 1) keeps its channel-3 route");
    check(!p60ch2 && !p61, "pattern 0 gained no extra route and the removed pattern is silent");
}

}  // namespace

int main() {
    test_wire_round_trip();
    test_fan_out_order_and_release();
    test_route_edits_release_on_recorded_channel();
    test_malformed_commands_change_nothing();
    test_draws_independent_of_routes();
    test_pattern_remove_compacts_routes();
    if (failures == 0) std::printf("example_instrument_routing: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
