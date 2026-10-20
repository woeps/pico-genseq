// performance-safe-foundation task 2.1 — canonical MIDI channel conversion and validation.
//
// Validates Requirements 5.1 and 5.9:
//   - one canonical internal MIDI channel representation covering all 16 protocol channels
//     (0..15) without aliases;
//   - user/boundary channels are 1..16 and convert losslessly to/from the canonical form;
//   - the OLD channel 0/1 alias is gone: user channel 1 maps to protocol 0 and NOTHING else
//     maps to protocol 0, so every one of the 16 user channels has a distinct protocol
//     channel and a distinct status-byte nibble;
//   - common::Pattern exposes the canonical channel derived from its stored 1..16 value.
//
// Standalone example (its own main(), no RapidCheck), so it builds and runs host-side
// unconditionally — it must be excluded from the property-test glob.

#include "common/MidiChannel.h"
#include "common/pattern.h"

#include <cstdint>
#include <cstdio>

using common::MidiChannel;
namespace mc = common::midi_channel;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Every user channel 1..16 round-trips through the canonical representation, maps to a
// distinct protocol channel 0..15, and produces a distinct status nibble. This is the
// all-16-channels, alias-free coverage required by Req 5.9.
void test_all_sixteen_channels_distinct_and_roundtrip() {
    bool protocolSeen[16] = {false};
    for (int user = mc::USER_MIN; user <= mc::USER_MAX; ++user) {
        check(mc::isValidUserChannel(user), "user channel 1..16 is valid");

        const MidiChannel canonical = mc::fromUserChannel(user);
        check(mc::isValidProtocolChannel(canonical.value), "canonical in 0..15");
        check(canonical.value == static_cast<uint8_t>(user - 1), "user N -> protocol N-1");

        // No two user channels collide onto the same protocol channel (alias-free).
        check(!protocolSeen[canonical.value], "protocol channel used by exactly one user channel");
        protocolSeen[canonical.value] = true;

        // Lossless round-trip back to the user channel.
        check(mc::toUserChannel(canonical) == user, "protocol -> user round-trip");

        // Status nibble equals the protocol value and stays in 0..15.
        check(mc::toStatusNibble(canonical) == canonical.value, "status nibble == protocol value");
        check(mc::toStatusNibble(canonical) <= 0x0F, "status nibble in nibble range");
    }
    for (int p = 0; p < 16; ++p) {
        check(protocolSeen[p], "every protocol channel 0..15 is reachable from some user channel");
    }
}

// The 0/1 alias is removed: user channel 1 (and only user channel 1) maps to protocol 0.
// Previously the emission path mapped both the invalid "channel 0" and user channel 1 to
// protocol 0; now there is a single owner of protocol 0.
void test_no_zero_one_alias() {
    check(mc::fromUserChannel(1).value == 0, "user channel 1 -> protocol 0");
    check(mc::fromUserChannel(2).value == 1, "user channel 2 -> protocol 1 (not aliased to 0)");
    check(mc::fromUserChannel(16).value == 15, "user channel 16 -> protocol 15");
    // An out-of-range value clamps into range rather than aliasing onto protocol 0.
    check(mc::fromUserChannel(0).value == 0, "below-range clamps to protocol 0 (user 1)");
    check(mc::fromUserChannel(99).value == 15, "above-range clamps to protocol 15 (user 16)");
    check(!mc::isValidUserChannel(0), "user channel 0 is invalid");
    check(!mc::isValidUserChannel(17), "user channel 17 is invalid");
    check(!mc::isValidProtocolChannel(16), "protocol channel 16 is invalid");
    check(!mc::isValidProtocolChannel(-1), "protocol channel -1 is invalid");
}

// Pattern exposes a canonical channel derived from its stored 1..16 user channel.
void test_pattern_canonical_channel() {
    common::Pattern pattern;  // default midiChannel == 1
    check(pattern.getMidiChannel() == 1, "default pattern user channel is 1");
    check(pattern.getCanonicalChannel().value == 0, "default pattern canonical channel is 0");

    for (int user = mc::USER_MIN; user <= mc::USER_MAX; ++user) {
        pattern.setMidiChannel(user);
        check(pattern.getCanonicalChannel() == mc::fromUserChannel(user),
              "pattern canonical channel tracks its user channel");
    }
}

}  // namespace

int main() {
    test_all_sixteen_channels_distinct_and_roundtrip();
    test_no_zero_one_alias();
    test_pattern_canonical_channel();

    if (failures == 0) {
        std::printf("example_canonical_channel: all checks passed\n");
        return 0;
    }
    std::printf("example_canonical_channel: %d check(s) failed\n", failures);
    return 1;
}
