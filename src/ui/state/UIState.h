#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::state {

enum class ViewId : uint8_t {
    INIT,
    SETTINGS,
    COUNT       // sentinel - keep last
};

constexpr size_t VIEW_COUNT = static_cast<size_t>(ViewId::COUNT);

struct UIState {
    ViewId currentView;
    uint8_t bpm;
    bool playing;
    int value;
    
    UIState() : currentView(ViewId::INIT), bpm(120), playing(false), value(0) {}
};

} // namespace ui::state
