#pragma once

#include <cstdint>

// Canonical MIDI channel representation (performance-safe-foundation task 2.1,
// Requirements 5.1 and 5.9).
//
// The MIDI wire protocol numbers channels 0..15 (the low nibble of a channel
// voice status byte). Humans and the UI number them 1..16. The previous
// firmware conflated the two: Pattern::midiChannel stored a 1-based value, the
// MIDI emission path masked it with `& 0x0F` and then applied
// `(channel > 0) ? channel - 1 : 0`, which ALIASED user channel 1 and the
// nonsense "channel 0" onto the same protocol channel 0 and silently wrapped
// user channel 16 to protocol 15 only by accident of the mask. Note ownership
// was also tracked by the pre-subtraction value, so the released channel did
// not match the emitted one.
//
// This header defines ONE canonical internal representation — a protocol
// channel 0..15 — plus explicit, lossless, alias-free conversions to and from
// the 1..16 user/boundary representation and the raw status-byte nibble. It is
// deliberately free of any Pico SDK include so it compiles unchanged in the
// firmware and the host test build.

namespace common {

// A validated protocol MIDI channel in the range 0..15. Construct it only
// through the boundary converters below so an out-of-range value can never
// reach the emission path.
struct MidiChannel {
    uint8_t value;  // canonical protocol channel, 0..15

    constexpr MidiChannel() : value(0) {}
    constexpr explicit MidiChannel(uint8_t protocolChannel) : value(protocolChannel) {}

    constexpr bool operator==(const MidiChannel& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const MidiChannel& other) const {
        return value != other.value;
    }
};

namespace midi_channel {

// Inclusive bounds for each representation.
constexpr uint8_t PROTOCOL_MIN = 0;    // first protocol channel
constexpr uint8_t PROTOCOL_MAX = 15;   // last protocol channel (all 16 covered)
constexpr uint8_t USER_MIN = 1;        // first user-facing channel
constexpr uint8_t USER_MAX = 16;       // last user-facing channel

// True when `userChannel` is a valid 1..16 user channel.
constexpr bool isValidUserChannel(int userChannel) {
    return userChannel >= USER_MIN && userChannel <= USER_MAX;
}

// True when `protocolChannel` is a valid 0..15 protocol channel.
constexpr bool isValidProtocolChannel(int protocolChannel) {
    return protocolChannel >= PROTOCOL_MIN && protocolChannel <= PROTOCOL_MAX;
}

// Convert a validated 1..16 user channel to the canonical protocol channel.
// Precondition: isValidUserChannel(userChannel). Out-of-range input is clamped
// into range rather than aliasing onto channel 0, so there is no 0/1 alias.
constexpr MidiChannel fromUserChannel(int userChannel) {
    const int clamped = userChannel < USER_MIN   ? USER_MIN
                        : userChannel > USER_MAX ? USER_MAX
                                                 : userChannel;
    return MidiChannel(static_cast<uint8_t>(clamped - 1));
}

// Convert a canonical protocol channel back to its 1..16 user channel.
constexpr int toUserChannel(MidiChannel channel) {
    return static_cast<int>(channel.value) + 1;
}

// The low nibble to OR into a channel-voice status byte. Always 0..15.
constexpr uint8_t toStatusNibble(MidiChannel channel) {
    return channel.value & 0x0F;
}

}  // namespace midi_channel

}  // namespace common
