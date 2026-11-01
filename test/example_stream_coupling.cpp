// generative-engine-expansion task 5.4 — stream coupling.
//
// Validates Requirements 5.1, 5.2, 5.3, 5.4: INDEPENDENT coupling advances every
// stream as today; LOCKED drives the velocity index from the pitch position via
// the defined modulo map (pitchPos % velocityCount), which is bounded and
// read-only (never advances/strands the velocity stream) and reconciles
// mismatched stream lengths.
//
// Drives the real Sequencer with a FakeMonotonicClock (same harness shape as
// example_emission_lens). Own main(), excluded from the glob.

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "sequencer.h"
#include "command.h"
#include "midi_messages.h"
#include "GenerationParameters.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace midi = sequencer::midi;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m; m.cmd = c; m.param1 = p1; return m;
}

// Ordered (pitch, velocity) of each NOTE_ON (velocity > 0).
std::vector<std::pair<uint8_t, uint8_t>> noteOns(const std::vector<uint8_t>& s) {
    std::vector<std::pair<uint8_t, uint8_t>> out;
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if ((s[i] & 0xF0) == midi::ChannelVoiceMessage::NOTE_ON && s[i + 2] > 0) {
            out.emplace_back(s[i + 1], s[i + 2]);
            i += 2;
        }
    }
    return out;
}

struct Harness {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};
    uint16_t bpm = 120;
    uint64_t base = 0;

    void setGate(const std::vector<bool>& g) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_GATE_SET, 0);
        m.gateCount = static_cast<uint16_t>(g.size()); m.gates = g;
        seq.processCommand(m);
    }
    void setPitch(const std::vector<uint8_t>& p) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_PITCH_SET, 0);
        m.pitchCount = static_cast<uint8_t>(p.size());
        m.pitchOrder = common::PlayingOrder::FORWARDS; m.pitches = p;
        seq.processCommand(m);
    }
    void setVelocity(const std::vector<uint8_t>& v) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_VELOCITY_SET, 0);
        m.velocityCount = static_cast<uint8_t>(v.size());
        m.velocityOrder = common::PlayingOrder::FORWARDS; m.velocities = v;
        seq.processCommand(m);
    }
    void play() { base = clock.nowMicros(); seq.processCommand(cmd(commands::Command::PLAY)); }
    uint64_t deadline(uint64_t n) const {
        return base + (n * 60ULL * 1000000ULL) / (bpm * 24ULL);
    }
    void tick(uint64_t n) { clock.setMicros(deadline(n)); seq.update(); }
};

// Gate pattern that fires an onset on each even tick: {T,F} repeated.
std::vector<bool> onsetTrain(int onsets) {
    std::vector<bool> g;
    for (int i = 0; i < onsets; ++i) { g.push_back(true); g.push_back(false); }
    return g;
}

// INDEPENDENT (default): pitch and velocity advance on their own positions, so
// across onsets each steps through its own set in lock-step with FORWARDS order.
void test_independent_is_today() {
    Harness h;
    h.setPitch({60, 62, 64});       // 3 pitches
    h.setVelocity({10, 20, 30});    // 3 velocities, same length
    h.setGate(onsetTrain(3));
    h.play();
    for (uint64_t n = 1; n <= 6; ++n) h.tick(n);

    const auto ons = noteOns(h.out.record());
    check(ons.size() == 3, "three onsets independent");
    if (ons.size() == 3) {
        // Each advances independently: pitch 60,62,64 and velocity 10,20,30.
        check(ons[0] == std::make_pair<uint8_t,uint8_t>(60, 10), "independent onset 0");
        check(ons[1] == std::make_pair<uint8_t,uint8_t>(62, 20), "independent onset 1");
        check(ons[2] == std::make_pair<uint8_t,uint8_t>(64, 30), "independent onset 2");
    }
}

// LOCKED with mismatched lengths: 3 pitches, 2 velocities. Velocity index =
// pitchPos % 2. Pitch positions at successive onsets are 0,1,2 (FORWARDS), so
// velocity indices are 0,1,0 -> velocities v0,v1,v0. This proves the modulo
// reconciliation (Req 5.4) and that velocity follows pitch, not its own stream.
void test_locked_follows_pitch_with_mismatch() {
    Harness h;
    h.setPitch({60, 62, 64});   // 3
    h.setVelocity({10, 20});    // 2 — mismatched
    h.setGate(onsetTrain(3));
    common::GenerationParameters gp;
    gp.coupling = common::CouplingMode::LOCKED;
    h.seq.setPatternGenerationParameters(0, gp);
    h.play();
    for (uint64_t n = 1; n <= 6; ++n) h.tick(n);

    const auto ons = noteOns(h.out.record());
    check(ons.size() == 3, "three onsets locked");
    if (ons.size() == 3) {
        // pitch pos 0 -> vel[0]=10 ; pos 1 -> vel[1]=20 ; pos 2 -> vel[2%2=0]=10
        check(ons[0] == std::make_pair<uint8_t,uint8_t>(60, 10), "locked onset 0: vel[0]");
        check(ons[1] == std::make_pair<uint8_t,uint8_t>(62, 20), "locked onset 1: vel[1]");
        check(ons[2] == std::make_pair<uint8_t,uint8_t>(64, 10), "locked onset 2: vel[2%2=0]");
    }
}

}  // namespace

int main() {
    test_independent_is_today();
    test_locked_follows_pitch_with_mismatch();

    if (failures == 0) {
        std::printf("example_stream_coupling: all checks passed\n");
        return 0;
    }
    std::printf("example_stream_coupling: %d check(s) failed\n", failures);
    return 1;
}
