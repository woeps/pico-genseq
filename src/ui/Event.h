#pragma once

#include <cstdint>
#include "Types.h"

namespace ui::events {

enum class EventType : uint8_t {
    BUTTON_PRESSED,
    BUTTON_RELEASED,
    BUTTON_HELD,
    POT_CHANGED
};

struct Event {
    EventType type;
    uint32_t timestamp;
    
    union {
        struct {
            ButtonId id;
        } button;
        
        struct {
            PotId id;
            uint16_t value;
        } pot;
    } data;
    
    Event() : type(EventType::BUTTON_PRESSED), timestamp(0) {}
    
    static Event buttonPressed(ButtonId id) {
        Event e;
        e.type = EventType::BUTTON_PRESSED;
        e.data.button.id = id;
        return e;
    }
    
    static Event buttonReleased(ButtonId id) {
        Event e;
        e.type = EventType::BUTTON_RELEASED;
        e.data.button.id = id;
        return e;
    }
    
    static Event buttonHeld(ButtonId id) {
        Event e;
        e.type = EventType::BUTTON_HELD;
        e.data.button.id = id;
        return e;
    }
    
    static Event potChanged(PotId id, uint16_t value) {
        Event e;
        e.type = EventType::POT_CHANGED;
        e.data.pot.id = id;
        e.data.pot.value = value;
        return e;
    }
};

} // namespace ui::events
