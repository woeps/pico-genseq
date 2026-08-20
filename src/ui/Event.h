#pragma once

#include <cstdint>
#include "Types.h"

namespace ui::events {

enum class EventType : uint8_t {
    KEY_PRESSED,
    KEY_RELEASED,
    KEY_HELD      // auto-repeat while the key stays down
};

struct Event {
    EventType type;
    uint32_t timestamp;

    union {
        struct {
            KeyId id;
            uint8_t mods;   // folded modifier bitmask, see ui::mod
        } key;
    } data;

    Event() : type(EventType::KEY_PRESSED), timestamp(0) {
        data.key.id = KeyId::NONE;
        data.key.mods = mod::NONE;
    }

    static Event keyPressed(KeyId id, uint8_t mods, uint32_t timestamp) {
        return make(EventType::KEY_PRESSED, id, mods, timestamp);
    }

    static Event keyReleased(KeyId id, uint8_t mods, uint32_t timestamp) {
        return make(EventType::KEY_RELEASED, id, mods, timestamp);
    }

    static Event keyHeld(KeyId id, uint8_t mods, uint32_t timestamp) {
        return make(EventType::KEY_HELD, id, mods, timestamp);
    }

private:
    static Event make(EventType type, KeyId id, uint8_t mods, uint32_t timestamp) {
        Event e;
        e.type = type;
        e.timestamp = timestamp;
        e.data.key.id = id;
        e.data.key.mods = mods;
        return e;
    }
};

} // namespace ui::events
