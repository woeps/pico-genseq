// separate-velocity-set-gate-set-and-note-set task 1.1 -- golden MIDI stream.
//
// Drives the real Sequencer with three patterns that each own DIFFERENT gate, pitch and
// velocity material (FORWARDS, PENDULUM and RANDOM orders) for 192 ticks and fingerprints the
// emitted byte stream (FNV-1a 64). The set-library refactor must reproduce this exact stream
// when every pattern references its own set of each type (Requirement 9.1, task 9.4).
//
// Run with --print to emit the current fingerprint after an intentional behaviour change.
// Standalone example (own main(), no RapidCheck); excluded from the property glob.

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "sequencer.h"
#include "command.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Fingerprint of the stream produced by the pre-refactor firmware logic.
constexpr uint64_t kGoldenHash = 0xe10306b07cf2b0ddULL;
constexpr uint32_t kGoldenLength = 1298;

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m;
    m.cmd = c;
    m.param1 = p1;
    return m;
}

struct Harness {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};
    uint64_t base = 0;

    void gate(uint8_t p, const std::vector<bool>& g) {
        auto m = cmd(commands::Command::PATTERN_GATE_SET, p);
        m.gateCount = static_cast<uint16_t>(g.size());
        m.gates = g;
        seq.processCommand(m);
    }
    void pitch(uint8_t p, const std::vector<uint8_t>& v, common::PlayingOrder o) {
        auto m = cmd(commands::Command::PATTERN_PITCH_SET, p);
        m.pitchCount = static_cast<uint8_t>(v.size());
        m.pitchOrder = o;
        m.pitches = v;
        seq.processCommand(m);
    }
    void velocity(uint8_t p, const std::vector<uint8_t>& v, common::PlayingOrder o) {
        auto m = cmd(commands::Command::PATTERN_VELOCITY_SET, p);
        m.velocityCount = static_cast<uint8_t>(v.size());
        m.velocityOrder = o;
        m.velocities = v;
        seq.processCommand(m);
    }
};

uint64_t fnv1a(const std::vector<uint8_t>& s) {
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : s) { h ^= b; h *= 1099511628211ULL; }
    return h;
}

}  // namespace

int main(int argc, char** argv) {
    using common::PlayingOrder;
    Harness h;
    h.seq.processCommand(cmd(commands::Command::PATTERN_ADD));
    h.seq.processCommand(cmd(commands::Command::PATTERN_ADD));

    h.gate(0, {true, false, true, false, true, true, false, false});
    h.pitch(0, {60, 64, 67, 72}, PlayingOrder::FORWARDS);
    h.velocity(0, {100, 80, 60}, PlayingOrder::FORWARDS);

    h.gate(1, {true, true, false, true, false, false});
    h.pitch(1, {48, 50, 53, 55, 57}, PlayingOrder::PENDULUM);
    h.velocity(1, {90, 70}, PlayingOrder::PENDULUM);

    h.gate(2, {true, false, false, true});
    h.pitch(2, {72, 74, 76, 79}, PlayingOrder::RANDOM);
    h.velocity(2, {110, 85, 64, 40}, PlayingOrder::RANDOM);

    h.base = h.clock.nowMicros();
    h.seq.processCommand(cmd(commands::Command::PLAY));
    const uint64_t bpm = 120;
    for (uint64_t n = 1; n <= 192; ++n) {
        h.clock.setMicros(h.base + (n * 60ULL * 1000000ULL) / (bpm * 24ULL));
        h.seq.update();
    }
    h.seq.processCommand(cmd(commands::Command::STOP));

    const uint64_t hash = fnv1a(h.out.record());
    const uint32_t len = static_cast<uint32_t>(h.out.record().size());
    if (argc > 1 && std::strcmp(argv[1], "--print") == 0) {
        std::printf("constexpr uint64_t kGoldenHash = 0x%016llxULL;\nconstexpr uint32_t kGoldenLength = %u;\n",
                    static_cast<unsigned long long>(hash), len);
        return 0;
    }
    if (hash != kGoldenHash || len != kGoldenLength) {
        std::printf("FAIL: stream hash 0x%016llx len %u != golden 0x%016llx len %u\n",
                    static_cast<unsigned long long>(hash), len,
                    static_cast<unsigned long long>(kGoldenHash), kGoldenLength);
        return 1;
    }
    std::printf("golden stream OK (%u bytes)\n", len);
    return 0;
}
