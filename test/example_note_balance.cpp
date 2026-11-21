// Hanging-note regression (bench report 2026-11-21: "editing while patterns play sometimes
// leaves hanging notes").
//
// Drives the real Sequencer with random edits while playing (gate / pitch / velocity sets,
// multicast sets, generation parameters incl. probability and scale-skip, activate/deactivate,
// add/remove patterns, BPM) and checks the MIDI byte stream per (channel, note):
//   - Note On and Note Off strictly alternate: never two Note Ons in a row for one key, never a
//     Note Off for a key that is off. Before the fix, two patterns on the same key sent
//     On, On, Off, which leaves a voice hanging on synths that start a voice per Note On.
//   - no key is on the wire unless a pattern owns a sounding note (nothing hangs while playing);
//   - after Stop every key is off.
// Plus the directed case that exposed it: two patterns in unison on shared sets.
// A note whose pitch is edited while it sounds is released by its recorded pitch; that is
// covered here by the random pitch edits and directly by example_sequencer_integration.
// Own main(), excluded from the glob.

#include "FakeMonotonicClock.h"
#include "IMidiOutput.h"
#include "command.h"
#include "sequencer.h"
#include "sequencer_telemetry.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// Parses the stream like a receiver and records protocol violations.
struct CheckingOut : sequencer::IMidiOutput {
    std::vector<uint8_t> msg;
    bool keyOn[16][128] = {{false}};
    long ons = 0, offs = 0, doubleOns = 0, strayOffs = 0;
    std::vector<uint8_t> log;  // channel-voice messages only, for the directed test
    void write(uint8_t b) override {
        if (b >= 0xF8) return;  // realtime: clock / start / stop
        if (b & 0x80) msg.clear();
        msg.push_back(b);
        if (msg.size() != 3) return;
        const uint8_t status = msg[0] & 0xF0, ch = msg[0] & 0x0F, note = msg[1] & 0x7F;
        log.insert(log.end(), msg.begin(), msg.end());
        if (status == 0x90 && msg[2] > 0) {
            ++ons;
            if (keyOn[ch][note]) ++doubleOns;
            keyOn[ch][note] = true;
        } else if (status == 0x80 || status == 0x90) {
            ++offs;
            if (!keyOn[ch][note]) ++strayOffs;
            keyOn[ch][note] = false;
        }
        msg.clear();
    }
    int keysOn() const {
        int c = 0;
        for (auto& row : keyOn) for (bool on : row) c += on ? 1 : 0;
        return c;
    }
};

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m;
    m.cmd = c;
    m.param1 = p1;
    return m;
}

struct RunResult {
    long ons = 0, offs = 0, doubleOns = 0, strayOffs = 0;
    int firstOrphanTick = -1;  // a key on the wire while no pattern owns a note
    int keysOnAfterStop = 0;
};

RunResult randomRun(unsigned seed) {
    std::mt19937 rng(seed);
    auto R = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    FakeMonotonicClock clock{1000};
    CheckingOut out;
    sequencer::Sequencer seq{out, clock};
    uint8_t n = 1;
    auto P = [&] { return static_cast<uint8_t>(R(0, n - 1)); };
    auto mask = [&] { return static_cast<uint16_t>(R(1, (1 << n) - 1)); };
    auto gates = [&] {
        std::vector<bool> g(static_cast<size_t>(R(3, 200)));
        const int density = R(5, 95);
        for (size_t i = 0; i < g.size(); ++i) g[i] = R(0, 99) < density;
        return g;
    };
    auto bytes = [&](int lo, int hi) {
        std::vector<uint8_t> v(static_cast<size_t>(R(1, 16)));
        for (auto& x : v) x = static_cast<uint8_t>(R(lo, hi));
        return v;
    };
    auto order = [&] { return static_cast<common::PlayingOrder>(R(0, 3)); };

    RunResult r;
    const uint64_t base = clock.nowMicros();
    seq.processCommand(cmd(commands::Command::PLAY));
    for (uint64_t t = 1; t <= 2000; ++t) {
        if (R(0, 99) < 25) {
            commands::CommandMessage m;
            switch (R(0, 11)) {
                case 0: case 1:
                    m = cmd(commands::Command::PATTERN_GATE_SET, P());
                    m.gates = gates(); m.gateCount = static_cast<uint16_t>(m.gates.size());
                    break;
                case 2:
                    m = cmd(commands::Command::PATTERN_GATE_SET_MULTI); m.patternMask = mask();
                    m.gates = gates(); m.gateCount = static_cast<uint16_t>(m.gates.size());
                    break;
                case 3: case 4:
                    m = cmd(commands::Command::PATTERN_PITCH_SET, P());
                    m.pitches = bytes(55, 66);  // narrow range: patterns collide on keys
                    m.pitchCount = static_cast<uint8_t>(m.pitches.size()); m.pitchOrder = order();
                    break;
                case 5:
                    m = cmd(commands::Command::PATTERN_PITCH_SET_MULTI); m.patternMask = mask();
                    m.pitches = bytes(55, 66);
                    m.pitchCount = static_cast<uint8_t>(m.pitches.size()); m.pitchOrder = order();
                    break;
                case 6:
                    m = cmd(commands::Command::PATTERN_VELOCITY_SET, P());
                    m.velocities = bytes(1, 127);
                    m.velocityCount = static_cast<uint8_t>(m.velocities.size());
                    m.velocityOrder = order();
                    break;
                case 7: {
                    common::GenerationParameters gp;
                    gp.patternProbability = static_cast<uint8_t>(R(0, 1) ? 100 : R(0, 100));
                    gp.scale = static_cast<common::ScaleId>(R(0, 2));
                    gp.quantize = static_cast<common::QuantizeMode>(R(0, 3));
                    gp.scaleRoot = static_cast<uint8_t>(R(0, 11));
                    gp.humanizeRange = static_cast<uint8_t>(R(0, 1) ? 0 : R(0, 64));
                    gp.seedSet = R(0, 1) != 0;
                    gp.seed = static_cast<uint32_t>(R(0, 1000));
                    gp.coupling = static_cast<common::CouplingMode>(R(0, 1));
                    gp.stepLength = static_cast<uint8_t>(R(0, 1) ? 6 : 0);
                    seq.setPatternGenerationParameters(P(), gp);
                    m = cmd(commands::Command::NOOP);
                    break;
                }
                case 8:
                    m = cmd(R(0, 1) ? commands::Command::PATTERN_ACTIVATE
                                    : commands::Command::PATTERN_DEACTIVATE, P());
                    break;
                case 9:
                    if (n < 15) { m = cmd(commands::Command::PATTERN_ADD); ++n; }
                    break;
                case 10:
                    if (n > 1) { m = cmd(commands::Command::PATTERN_REMOVE, P()); --n; }
                    break;
                case 11:
                    m = cmd(commands::Command::BPM_SET, static_cast<uint8_t>(R(40, 255)));
                    break;
            }
            seq.processCommand(m);
            // One Instrument on channel 1 plays every pattern (the pre-Instrument behaviour).
            if (m.cmd == commands::Command::PATTERN_ADD) {
                seq.applyInstrument(0, 0, static_cast<uint16_t>((1u << n) - 1u));
            }
        }
        clock.setMicros(base + (t * 60ULL * 1000000ULL) / (120ULL * 24ULL));
        seq.update();
        seq.update();  // no tick due: republishes g_soundingNotes after this tick
        // Each owner holds one key, so more keys on the wire than owners means an orphan.
        if (r.firstOrphanTick < 0 && out.keysOn() > static_cast<int>(sequencer::g_soundingNotes)) {
            r.firstOrphanTick = static_cast<int>(t);
        }
    }
    seq.processCommand(cmd(commands::Command::STOP));
    r.ons = out.ons; r.offs = out.offs; r.doubleOns = out.doubleOns; r.strayOffs = out.strayOffs;
    r.keysOnAfterStop = out.keysOn();
    return r;
}

void test_random_edits_never_hang_a_note() {
    long ons = 0, doubles = 0, strays = 0, orphans = 0, leaks = 0;
    for (unsigned seed = 1; seed <= 60; ++seed) {
        const RunResult r = randomRun(seed);
        ons += r.ons;
        doubles += r.doubleOns;
        strays += r.strayOffs;
        if (r.firstOrphanTick >= 0) ++orphans;
        if (r.keysOnAfterStop != 0) ++leaks;
        if (r.ons != r.offs) {
            std::printf("seed %u: ons %ld offs %ld doubles %ld\n", seed, r.ons, r.offs, r.doubleOns);
        }
    }
    std::printf("random edits: %ld note-ons over 60 runs\n", ons);
    check(ons > 10000, "the random runs actually play notes");
    check(doubles == 0, "never two Note Ons in a row for one key");
    check(strays == 0, "never a Note Off for a key that is off");
    check(orphans == 0, "no key stays on while no pattern owns a note");
    check(leaks == 0, "every key is off after Stop");
}

// The case from the bench: two patterns in unison (same gate, pitch and velocity material).
void test_unison_patterns_alternate_on_off() {
    FakeMonotonicClock clock{1000};
    CheckingOut out;
    sequencer::Sequencer seq{out, clock};
    seq.processCommand(cmd(commands::Command::PATTERN_ADD));
    seq.applyInstrument(0, 0, 0b11);
    commands::CommandMessage g = cmd(commands::Command::PATTERN_GATE_SET_MULTI);
    g.patternMask = 0b11;
    g.gates = {true, true, false, false, false, false};
    g.gateCount = 6;
    seq.processCommand(g);
    commands::CommandMessage p = cmd(commands::Command::PATTERN_PITCH_SET_MULTI);
    p.patternMask = 0b11;
    p.pitches = {60};
    p.pitchCount = 1;
    seq.processCommand(p);
    const uint64_t base = clock.nowMicros();
    seq.processCommand(cmd(commands::Command::PLAY));
    for (uint64_t t = 1; t <= 24; ++t) {
        clock.setMicros(base + (t * 60ULL * 1000000ULL) / (120ULL * 24ULL));
        seq.update();
    }
    seq.processCommand(cmd(commands::Command::STOP));

    // Expected per onset: On(A), Off, On(B) (retrigger closes the key first), then a single Off
    // when the last owner releases. Default velocities are 100 for the first step.
    const std::vector<uint8_t> firstNote = {0x90, 60, 100, 0x80, 60, 0, 0x90, 60, 100, 0x80, 60, 0};
    check(out.log.size() >= firstNote.size() &&
              std::vector<uint8_t>(out.log.begin(), out.log.begin() + 12) == firstNote,
          "unison onset: On, Off, On, then one Off");
    check(out.doubleOns == 0 && out.strayOffs == 0, "unison stream alternates per key");
    check(out.ons == out.offs && out.ons >= 8, "every Note On is matched by a Note Off");
    check(out.keysOn() == 0, "nothing held after Stop");
}

}  // namespace

int main() {
    test_unison_patterns_alternate_on_off();
    test_random_edits_never_hang_a_note();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("note balance OK\n");
    return 0;
}
