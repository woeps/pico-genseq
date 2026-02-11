#pragma once

namespace sequencer {
    namespace midi {
        // MIDI Channel Voice Messages (most significant nibble)
        // Using traditional enum for implicit conversion to uint8_t
        enum ChannelVoiceMessage : uint8_t {
            NOTE_OFF = 0x80,
            NOTE_ON = 0x90,
            POLY_AFTERTOUCH = 0xA0,
            CONTROL_CHANGE = 0xB0,
            PROGRAM_CHANGE = 0xC0,
            CHANNEL_AFTERTOUCH = 0xD0,
            PITCH_BEND = 0xE0
        };

        // MIDI System Common Messages
        // Using traditional enum for implicit conversion to uint8_t
        enum SystemCommonMessage : uint8_t {
            SYSEX_START = 0xF0,
            MTC_QUARTER_FRAME = 0xF1,
            SONG_POSITION = 0xF2,
            SONG_SELECT = 0xF3,
            TUNE_REQUEST = 0xF6,
            SYSEX_END = 0xF7
        };

        // MIDI System Real-Time Messages
        // Using traditional enum for implicit conversion to uint8_t
        enum SystemRealTimeMessage : uint8_t {
            TIMING_CLOCK = 0xF8,
            START = 0xFA,
            CONTINUE = 0xFB,
            STOP = 0xFC,
            ACTIVE_SENSING = 0xFE,
            SYSTEM_RESET = 0xFF
        };
    }
} // namespace sequencer
