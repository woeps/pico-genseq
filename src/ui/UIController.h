#pragma once

#include <array>
#include <memory>
#include "hardware/HardwareConfig.h"
#include "hardware/Led.h"
#include "hardware/LedMatrix.h"
#include "hardware/UsbKeyboard.h"
#include "views/IView.h"
#include "views/InitView.h"
#include "views/SettingsView.h"
#include "views/PatternsView.h"
#include "views/GateSetView.h"
#include "views/PitchSetView.h"
#include "views/VelocitySetView.h"
#include "state/UIState.h"
#include "../persistence/FlashStore.h"
#include "../persistence/PersistenceManager.h"

namespace ui {

class UIController {
public:
    UIController(const HardwareConfig& config);
    ~UIController();

    void initialize();
    void update();

private:
    const HardwareConfig& config;

    // Hardware
    std::unique_ptr<hardware::Led> led;
    std::unique_ptr<hardware::LedMatrix> ledMatrix;
    std::unique_ptr<hardware::UsbKeyboard> keyboard;

    // Views (heap-allocated but fixed at initialization, no dynamic allocation after)
    std::unique_ptr<InitView> initView;
    std::unique_ptr<SettingsView> settingsView;
    std::unique_ptr<PatternsView> patternsView;
    std::unique_ptr<GateSetView> gateSetView;
    std::unique_ptr<PitchSetView> pitchSetView;
    std::unique_ptr<VelocitySetView> velocitySetView;
    std::array<IView*, state::VIEW_COUNT> views{};
    IView* activeView;

    // Durable settings persistence (persist-settings feature). The FlashStore
    // owns the reserved flash region; the PersistenceManager orchestrates the
    // boot load and Ctrl+S save over it. Constructed in initialize() so the
    // manager can bind to the store by reference in a well-defined order.
    std::unique_ptr<persistence::FlashStore> flashStore;
    std::unique_ptr<persistence::PersistenceManager> persistenceManager;

    // --- Save-request draining / feedback timing (persist-settings, task 7.2) ---
    // Guards against a duplicate/re-entrant save being launched while one is
    // already running (Req 1.4). Because save() is synchronous on this
    // single-core UI loop (it parks core1 and performs the blocking flash
    // write), this flag is set immediately before the call and cleared right
    // after, so the debounce window is effectively the duration of the flash
    // write itself.
    bool saveInProgress = false;

    // Whether a feedback banner is currently active and the time (ms since
    // boot) it was set. bannerSetMs is the timestamp captured when a banner is
    // written into the UIState; task 7.3 consumes it to auto-dismiss the banner
    // between 1 s and 2 s after it appeared (Req 7.3). Populated here so the
    // Display-rendering seam in 7.3 only has to read them.
    bool bannerActive = false;
    uint32_t bannerSetMs = 0;

    // Tracks whether the currently-active banner has already been rendered to
    // the LED matrix, so updateSaveBanner() paints it exactly once per banner
    // rather than every tick (idempotent per banner, keeps the loop cheap and
    // avoids fighting the active view — persist-settings, task 7.3).
    bool bannerRendered = false;

    void onStateChanged(const state::UIState& newState);

    // Drain a pending saveRequested flag, perform the (synchronous) save, and
    // record the resulting banner + timestamp (persist-settings, task 7.2).
    void drainSaveRequest();

    // Render the active save/boot banner on the LED matrix and auto-dismiss it
    // after a bounded interval (persist-settings, task 7.3). See the .cpp for
    // the deviation note: feedback is presented on the LedMatrix that
    // UIController actually owns rather than the design's (uninstantiated) LCD.
    void updateSaveBanner();
};

} // namespace ui
