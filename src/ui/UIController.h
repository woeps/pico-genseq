#pragma once

#include <array>
#include <memory>
#include "hardware/HardwareConfig.h"
#include "hardware/Led.h"
#include "hardware/LedMatrix.h"
#include "hardware/UsbKeyboard.h"
#include "hardware/HostControl.h"
#include "views/IView.h"
#include "views/InitView.h"
#include "views/SettingsView.h"
#include "views/PatternsView.h"
#include "views/GateSetView.h"
#include "views/PitchSetView.h"
#include "views/VelocitySetView.h"
#include "state/UIState.h"
#include "../persistence/Mb85rsFram.h"
#include "../persistence/Mb85rsFramSpi.h"
#include "../persistence/RecordStore.h"

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

    // Host-control command channel for autonomous bench testing (OFF unless
    // GENSEQ_HOST_CONTROL). Reads "H:" lines from stdin and synthesizes the
    // same ui::events::Event values a keypress would, through the identical
    // dispatch() seam. Zero-cost when the flag is 0 (poll() compiles away).
    hardware::HostControl hostControl;

    // Views (heap-allocated but fixed at initialization, no dynamic allocation after)
    std::unique_ptr<InitView> initView;
    std::unique_ptr<SettingsView> settingsView;
    std::unique_ptr<PatternsView> patternsView;
    std::unique_ptr<GateSetView> gateSetView;
    std::unique_ptr<PitchSetView> pitchSetView;
    std::unique_ptr<VelocitySetView> velocitySetView;
    std::array<IView*, state::VIEW_COUNT> views{};
    IView* activeView;

    // Durable settings persistence (persist-settings feature). The v1 backend
    // is an external SPI FRAM (MB85RS2MTA, task 9.3/9.4): Mb85rsFramSpi is the
    // SPI1 transport, Mb85rsFram is the byte-addressed IFlashStore over it, and
    // RecordStore adds the A/B transactional-commit layer (task 10.1/10.2) used
    // for both the boot load and the Ctrl+S save. Constructed in initialize()
    // in dependency order so each binds its predecessor by reference. FRAM is
    // byte-writable with no erase and no XIP-disable, so a save never parks the
    // cores (the transparency proven in task 9.3).
    std::unique_ptr<persistence::Mb85rsFramSpi> framSpi;
    std::unique_ptr<persistence::Mb85rsFram> framStore;
    std::unique_ptr<persistence::RecordStore> recordStore;

    // Scratch for the save path's serialized config (~666 bytes). Held as a
    // member, NOT a stack local: core0's stack is 2 KB and the save runs deep
    // in the TinyUSB->keyboard->dispatch call chain, where a large stack local
    // (alongside the UIState copies) overflowed the stack and double-faulted
    // core0. Only ever touched on core0 in drainSaveRequest().
    persistence::PersistableConfig framConfig_{};

    // --- Literal timing metrics (task 12.3/12.4) -----------------------------
    // Measured directly on the live path so H:dump can report real numbers
    // instead of a proxy. lastSaveMicros_ is the wall-clock duration of the most
    // recent live RecordStore::commit (the save-time gap, vs the <=100 ms
    // limit). maxUiGapMicros_ is the longest observed interval between UI-loop
    // update() calls (the UI responsiveness gap, vs the <=50 ms limit).
    uint32_t lastSaveMicros_ = 0;
    uint32_t maxUiGapMicros_ = 0;
    uint64_t lastUpdateMicros_ = 0;

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
