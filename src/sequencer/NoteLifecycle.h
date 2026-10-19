#pragma once

#include <cstdint>

#include "../common/MidiChannel.h"
#include "../common/PatternId.h"
#include "IMidiOutput.h"
#include "midi_messages.h"

// Centralized note lifecycle (performance-safe-foundation task 2.3,
// Requirements 5.1-5.5, 5.8).
//
// Every Note On / Note Off in the system passes through ONE NoteLifecycle
// service so that ownership and release are always consistent:
//
//   - Each monophonic Pattern runtime owns AT MOST ONE sounding note at a time
//     (OwnedNote), recorded by its canonical channel+note at the moment of
//     Note On. The pattern's later release uses THIS recorded identity, never
//     the pattern's (possibly edited) current pitch/channel (Req 5.1, 5.2).
//
//   - An AGGREGATE reference count per (channel, note) pair tracks how many
//     patterns currently sound that exact wire key. When several patterns hit
//     the same key, every requested Note On is emitted (the approved
//     independent-retrigger policy, Req 5.3) and the final Note Off is emitted
//     only when the last owner releases and the count reaches zero (Req 5.4,
//     5.5).
//
//   - releasePattern() releases exactly one pattern's owned note (used by a
//     falling gate edge, mute, or remove at its effective boundary, Req 5.6).
//     releaseAll() / panic() release everything and leave both per-pattern and
//     aggregate ownership empty (Stop / fail-closed, Req 5.8). panic()
//     additionally force-clears any residual aggregate counts so a leaked count
//     can never strand a note.
//
// Storage is fixed-capacity and allocation-free. This header is SDK-free (it
// pulls in only IMidiOutput, the canonical channel/id types, and the MIDI byte
// tables) so it compiles unchanged in firmware and host tests.

namespace sequencer {

class NoteLifecycle {
public:
    explicit NoteLifecycle(IMidiOutput& output) : output_(output) {}

    // Emit Note On for `pattern` and record ownership of (channel, note).
    //
    // If `pattern` already owns a different sounding note, that previous note is
    // released first (a monophonic pattern can only hold one note), so an
    // onset without a matching falling edge can never strand a note. Every
    // Note On is emitted on the wire, including a retrigger of a key another
    // pattern already sounds (Req 5.3).
    void noteOn(common::PatternId pattern, common::MidiChannel channel, uint8_t note,
                uint8_t velocity) {
        if (!pattern.valid() || pattern.index >= common::PATTERN_CAPACITY) return;
        note &= 0x7F;

        OwnedNote& owned = owned_[pattern.index];
        if (owned.sounding) {
            // Replace: release the old note first so ownership stays monophonic.
            releaseOwned(owned);
        }

        owned.sounding = true;
        owned.generation = pattern.generation;
        owned.channel = channel;
        owned.note = note;

        ++aggregate_[channel.value][note];  // this pattern now sounds the key
        emitNoteOn(channel, note, velocity);
    }

    // Release the note owned by `pattern`, if any, using the RECORDED identity.
    // The generation must match the current owner so a stale id does not release
    // a note a newer occupant started (defensive; the scheduler already resolves
    // live ids). The final Note Off is emitted only when the aggregate count for
    // the key reaches zero.
    void releasePattern(common::PatternId pattern) {
        if (pattern.index >= common::PATTERN_CAPACITY) return;
        OwnedNote& owned = owned_[pattern.index];
        if (!owned.sounding) return;
        if (owned.generation != pattern.generation) return;  // stale: not this owner
        releaseOwned(owned);
    }

    // Release every owned note and clear all ownership (Stop / normal all-off).
    void releaseAll() {
        for (uint8_t i = 0; i < common::PATTERN_CAPACITY; ++i) {
            if (owned_[i].sounding) releaseOwned(owned_[i]);
        }
    }

    // Fail-closed panic: release everything AND force-clear any residual
    // aggregate counts, guaranteeing zero per-pattern and aggregate ownership
    // remains even if a count was somehow leaked (Req 5.8).
    void panic() {
        releaseAll();
        for (uint16_t ch = 0; ch < 16; ++ch) {
            for (uint16_t n = 0; n < 128; ++n) {
                if (aggregate_[ch][n] != 0) {
                    aggregate_[ch][n] = 0;
                    emitNoteOff(common::MidiChannel(static_cast<uint8_t>(ch)),
                                static_cast<uint8_t>(n));
                }
            }
        }
    }

    // --- Observers (host tests / validation) ---------------------------------
    bool patternSounding(common::PatternId pattern) const {
        if (pattern.index >= common::PATTERN_CAPACITY) return false;
        const OwnedNote& o = owned_[pattern.index];
        return o.sounding && o.generation == pattern.generation;
    }

    uint16_t aggregateCount(common::MidiChannel channel, uint8_t note) const {
        return aggregate_[channel.value][note & 0x7F];
    }

    // Number of patterns currently holding a sounding note. Zero after Stop /
    // fault (the post-Stop invariant). Published cross-core for the literal
    // stuck-note validation metric (task 12.4).
    uint8_t soundingCount() const {
        uint8_t c = 0;
        for (uint8_t i = 0; i < common::PATTERN_CAPACITY; ++i) {
            if (owned_[i].sounding) ++c;
        }
        return c;
    }

    // True only when NO per-pattern note and NO aggregate count remain. The
    // post-Stop / post-fault invariant (Req 5.8).
    bool ownershipEmpty() const {
        for (uint8_t i = 0; i < common::PATTERN_CAPACITY; ++i) {
            if (owned_[i].sounding) return false;
        }
        for (uint16_t ch = 0; ch < 16; ++ch) {
            for (uint16_t n = 0; n < 128; ++n) {
                if (aggregate_[ch][n] != 0) return false;
            }
        }
        return true;
    }

private:
    struct OwnedNote {
        bool sounding = false;
        uint32_t generation = 0;  // generation of the PatternId that started it
        common::MidiChannel channel{};
        uint8_t note = 0;
    };

    // Release one owned note record: decrement the aggregate count and emit the
    // final Note Off only when the last owner lets go.
    void releaseOwned(OwnedNote& owned) {
        const common::MidiChannel channel = owned.channel;
        const uint8_t note = owned.note;
        owned.sounding = false;

        uint16_t& count = aggregate_[channel.value][note];
        if (count > 0) {
            --count;
            if (count == 0) emitNoteOff(channel, note);
        }
    }

    void emitNoteOn(common::MidiChannel channel, uint8_t note, uint8_t velocity) {
        output_.write(midi::ChannelVoiceMessage::NOTE_ON |
                      common::midi_channel::toStatusNibble(channel));
        output_.write(note & 0x7F);
        output_.write(velocity & 0x7F);
    }

    void emitNoteOff(common::MidiChannel channel, uint8_t note) {
        output_.write(midi::ChannelVoiceMessage::NOTE_OFF |
                      common::midi_channel::toStatusNibble(channel));
        output_.write(note & 0x7F);
        output_.write(0);
    }

    IMidiOutput& output_;
    OwnedNote owned_[common::PATTERN_CAPACITY];
    uint16_t aggregate_[16][128] = {{0}};  // reference count per (channel, note)
};

}  // namespace sequencer
