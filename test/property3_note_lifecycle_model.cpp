// performance-safe-foundation task 2.6 — model-based note lifecycle property.
//
// Validates Requirements 5.1-5.10, 11.2, 11.3 against sequencer::NoteLifecycle.
//
// A random trace of lifecycle operations (noteOn / releasePattern / releaseAll /
// panic) over several monophonic patterns is run through the real NoteLifecycle
// while an independent REFERENCE MODEL tracks, per pattern, the single note it
// should own and, per (channel,note), the aggregate owner count. After each
// operation the property asserts the service agrees with the model on:
//   - which pattern is sounding and the exact recorded (channel,note) — covers
//     overlap (5.3), retrigger (every Note On emitted), monophonic replacement,
//     release-by-recorded-identity (5.1/5.2), and shared-key aggregate release
//     (5.4/5.5): the final Note Off is emitted only when the last owner lets go.
//   - the EMITTED WIRE STREAM equals the model's expected Note On/Off byte
//     stream exactly (value and order).
//
// Crucially (Req 11.3): the property appends a terminating panic and asserts
// ownershipEmpty() holds after it — no trace ending in Stop/fault leaves any
// per-pattern or aggregate ownership (5.8).

#include "NoteLifecycle.h"
#include "RecordingMidiOutput.h"
#include "midi_messages.h"
#include "../common/MidiChannel.h"
#include "../common/PatternId.h"

#include <rapidcheck.h>

#include <array>
#include <cstdint>
#include <vector>

namespace midi = sequencer::midi;

namespace {

// A small fixed set of patterns so overlaps and shared keys actually occur.
constexpr uint8_t kPatterns = 4;

enum class Op : uint8_t { NoteOn, ReleasePattern, ReleaseAll, Panic };

struct GenOp {
    Op op;
    uint8_t pattern;   // 0..kPatterns-1
    uint8_t channel;   // 0..15 canonical
    uint8_t note;      // 0..127
    uint8_t velocity;  // 0..127
};

// Reference model: per-pattern owned note + aggregate (channel,note) count, and
// the expected emitted byte stream.
struct Model {
    struct Owned { bool sounding = false; uint8_t channel = 0; uint8_t note = 0; };
    std::array<Owned, kPatterns> owned{};
    uint16_t aggregate[16][128] = {{0}};
    std::vector<uint8_t> wire;

    void emitOn(uint8_t ch, uint8_t note, uint8_t vel) {
        wire.push_back(static_cast<uint8_t>(midi::ChannelVoiceMessage::NOTE_ON | (ch & 0x0F)));
        wire.push_back(note & 0x7F);
        wire.push_back(vel & 0x7F);
    }
    void emitOff(uint8_t ch, uint8_t note) {
        wire.push_back(static_cast<uint8_t>(midi::ChannelVoiceMessage::NOTE_OFF | (ch & 0x0F)));
        wire.push_back(note & 0x7F);
        wire.push_back(0);
    }
    void releaseOwned(Owned& o) {
        if (!o.sounding) return;
        o.sounding = false;
        uint16_t& c = aggregate[o.channel][o.note];
        if (c > 0) { --c; if (c == 0) emitOff(o.channel, o.note); }
    }
    void noteOn(uint8_t p, uint8_t ch, uint8_t note, uint8_t vel) {
        ch &= 0x0F; note &= 0x7F; vel &= 0x7F;
        Owned& o = owned[p];
        if (o.sounding) releaseOwned(o);        // monophonic replacement
        o.sounding = true; o.channel = ch; o.note = note;
        ++aggregate[ch][note];
        emitOn(ch, note, vel);                  // every Note On emitted (5.3)
    }
    void releasePattern(uint8_t p) { releaseOwned(owned[p]); }
    void releaseAll() { for (auto& o : owned) releaseOwned(o); }
    void panic() { releaseAll(); /* model never leaks an aggregate count */ }
    bool empty() const {
        for (const auto& o : owned) if (o.sounding) return false;
        for (auto& row : aggregate) for (uint16_t v : row) if (v != 0) return false;
        return true;
    }
};

}  // namespace

namespace rc {
template <>
struct Arbitrary<GenOp> {
    static Gen<GenOp> arbitrary() {
        return gen::build<GenOp>(
            gen::set(&GenOp::op, gen::weightedElement<Op>({
                {5, Op::NoteOn},         // favor Note On so overlaps/retriggers happen
                {3, Op::ReleasePattern},
                {1, Op::ReleaseAll},
                {1, Op::Panic},
            })),
            gen::set(&GenOp::pattern, gen::inRange<uint8_t>(0, kPatterns)),
            // Narrow channel/note ranges so shared (channel,note) keys collide
            // across patterns and the aggregate-refcount path is exercised.
            gen::set(&GenOp::channel, gen::inRange<uint8_t>(0, 3)),
            gen::set(&GenOp::note, gen::inRange<uint8_t>(60, 64)),
            gen::set(&GenOp::velocity, gen::inRange<uint8_t>(1, 128)));
    }
};
}  // namespace rc

bool runProperty3() {
    bool ok = true;

    ok &= rc::check(
        "Property 3 (note lifecycle): service matches the reference model step by "
        "step, and every trace terminated by panic leaves zero ownership",
        [](const std::vector<GenOp>& ops) {
            RecordingMidiOutput out;
            sequencer::NoteLifecycle nl(out);
            Model model;
            // Stable PatternIds, generation 1, one per slot.
            std::array<common::PatternId, kPatterns> ids;
            for (uint8_t i = 0; i < kPatterns; ++i) ids[i] = common::PatternId(i, 1);

            auto applyBoth = [&](const GenOp& g) {
                switch (g.op) {
                    case Op::NoteOn:
                        nl.noteOn(ids[g.pattern], common::MidiChannel(g.channel & 0x0F),
                                  g.note, g.velocity);
                        model.noteOn(g.pattern, g.channel, g.note, g.velocity);
                        break;
                    case Op::ReleasePattern:
                        nl.releasePattern(ids[g.pattern]);
                        model.releasePattern(g.pattern);
                        break;
                    case Op::ReleaseAll:
                        nl.releaseAll();
                        model.releaseAll();
                        break;
                    case Op::Panic:
                        nl.panic();
                        model.panic();
                        break;
                }
            };

            for (const auto& g : ops) {
                applyBoth(g);
                // Per-pattern sounding state agrees with the model after each op.
                for (uint8_t i = 0; i < kPatterns; ++i) {
                    RC_ASSERT(nl.patternSounding(ids[i]) == model.owned[i].sounding);
                }
                // Emitted wire stream agrees exactly (value and order).
                RC_ASSERT(out.record() == model.wire);
            }

            // Req 11.3 / 5.8: terminate the trace with a fail-closed all-off and
            // assert BOTH the service and model have zero remaining ownership.
            nl.panic();
            model.panic();
            RC_ASSERT(out.record() == model.wire);
            RC_ASSERT(nl.ownershipEmpty());
            RC_ASSERT(model.empty());
        });

    return ok;
}
