#include "UIController.h"
#include "state/Reducer.h"
#include "state/StateManager.h"
#include "state/PersistMapping.h"
#include "../persistence/PersistableConfig.h"
#include "../persistence/BootBannerPolicy.h"
#include "pico/time.h"
#include <cstdio>

namespace {
// Completion / watchdog bounds from the design (Req 1.1, 7.5). save() here is
// synchronous and bounded by the RP2040 flash erase+program time (a handful of
// milliseconds, far under COMPLETION_LIMIT_MS), so these are defensive guards:
// if a save ever exceeded the completion limit, we still report a FAILURE
// banner rather than trusting a late outcome.
constexpr uint32_t SAVE_COMPLETION_LIMIT_MS = 2000;

// --- Banner auto-dismiss + colors (persist-settings, task 7.3) ---------------
//
// DEVIATION FROM DESIGN: the design's "Feedback via Display" component assumed a
// 16x2 HD44780 I2C LCD (`hardware::Display`). That class exists but is NOT
// instantiated anywhere and is NOT wired into UIController; UIController owns
// only `hardware::Led` (onboard) and `hardware::LedMatrix` (WS2812 16x16), and
// src/config/pins.h defines no I2C/SDA/SCL pins or LCD address. Rather than
// invent hardware pins (which could break the build or misbehave on-device),
// the banner feedback is presented on the LedMatrix that UIController actually
// owns. This satisfies the requirement's INTENT — distinct, auto-dismissed
// success / failure / not-loaded feedback (Req 7.1, 7.2, 7.3, 6.7).
//
// Dismiss threshold sits in the required [1000, 2000] ms window (Req 7.3).
constexpr uint32_t BANNER_DISMISS_MS = 1500;

// Colors are 0xXXGGRRBB (matches the existing views' convention). The three
// banners are visually DISTINCT (Req 7.2): success is green, failure is red,
// and the boot "config not loaded" case is amber (green + red, no blue).
constexpr uint32_t BANNER_COLOR_SUCCESS = 0x00FF0000;    // green
constexpr uint32_t BANNER_COLOR_FAILURE = 0x0000FF00;    // red
constexpr uint32_t BANNER_COLOR_NOT_LOADED = 0x00FFFF00; // amber (green+red)
} // namespace

namespace ui {

UIController::UIController(const HardwareConfig& config)
    : config(config), activeView(nullptr) {}

UIController::~UIController() = default;

void UIController::initialize()
{
    printf("Initializing UI Controller...\n");

    // Create hardware
    led = std::make_unique<hardware::Led>(config.ledPin);
    ledMatrix = std::make_unique<hardware::LedMatrix>(config.ledMatrixPin);

    // Create views (allocated once at initialization)
    initView = std::make_unique<InitView>(*led, *ledMatrix);
    settingsView = std::make_unique<SettingsView>(*led, *ledMatrix);
    patternsView = std::make_unique<PatternsView>(*ledMatrix);
    gateSetView = std::make_unique<GateSetView>(*ledMatrix);
    pitchSetView = std::make_unique<PitchSetView>(*ledMatrix);
    velocitySetView = std::make_unique<VelocitySetView>(*ledMatrix);

    // Initialize view array
    views[static_cast<size_t>(state::ViewId::INIT)] = initView.get();
    views[static_cast<size_t>(state::ViewId::SETTINGS)] = settingsView.get();
    views[static_cast<size_t>(state::ViewId::PATTERNS)] = patternsView.get();
    views[static_cast<size_t>(state::ViewId::GATE_SET)] = gateSetView.get();
    views[static_cast<size_t>(state::ViewId::PITCH_SET)] = pitchSetView.get();
    views[static_cast<size_t>(state::ViewId::VELOCITY_SET)] = velocitySetView.get();

    // Register views with StateManager so it can look up active view during dispatch
    state::getStateManager().setViews(views);

    // Subscribe to state changes for view switching. Subscribe BEFORE the boot
    // load so that setState() below drives a render of the restored state.
    state::getStateManager().subscribe([this](const state::UIState& newState) {
        onStateChanged(newState);
    });

    // Load-and-restore on boot (persist-settings feature, task 7.1).
    //
    // Ordering guarantee (Req 5.1): core1's Sequencer launches with
    // playing == false and emits no MIDI until it receives a PLAY command,
    // which only ever originates from a user pressing SPACE. Keyboard input is
    // brought up at the very end of initialize(), AFTER this block, so the
    // load and the sync-command replay always complete before any PLAY can be
    // dispatched and before the sequencer produces any MIDI. We must not
    // dispatch PLAY here.
    flashStore = std::make_unique<persistence::FlashStore>();
    persistenceManager =
        std::make_unique<persistence::PersistenceManager>(*flashStore);

    persistence::PersistableConfig cfg{};
    const persistence::LoadStatus status = persistenceManager->load(cfg);

    // Start from the StateManager's current (default) UIState.
    state::UIState bootState = state::getStateManager().getState();

    if (status == persistence::LoadStatus::OK) {
        // Valid saved config: fold it into the UIState mirror, install it as the
        // authoritative state, then replay the sync commands to seed core1 from
        // the restored configuration (Req 5.3, 5.4).
        state::applyPersistableConfig(bootState, cfg);
        state::getStateManager().setState(bootState);

        const state::UIState& restored = state::getStateManager().getState();
        state::restoreSequencerFromState(restored);
    } else {
        // No valid saved config. Keep the firmware defaults and leave the
        // Storage_Region untouched (load is read-only).
        //
        // Only surface the "saved config not loaded" banner when a
        // WELL-FORMED record of this firmware's own format existed but its
        // payload was corrupt - i.e. CRC_MISMATCH, which means the magic and
        // format version both matched (so it genuinely is a GenSeq save of this
        // version) yet the contents did not verify. That is the one case the
        // user should know about (a real save that could not be restored).
        //
        // Every other non-OK status is treated as "no usable save present" and
        // starts silently with defaults, showing NO banner:
        //   - BAD_MAGIC / ABSENT : erased or never-written region (normal first
        //     boot), or arbitrary stale bytes left in the region that are not a
        //     GenSeq record. Reflashing the firmware does NOT erase this
        //     top-of-flash region, so leftover development data can land here;
        //     it is not the user's "saved config" and must not nag them.
        //   - BAD_VERSION / TRUNCATED : an unrecognized or malformed span,
        //     indistinguishable from junk - not treated as the user's save.
        if (persistence::shouldWarnOnBootLoad(status)) {
            bootState.saveBanner = state::SaveBanner::NOT_LOADED;
        }
        state::getStateManager().setState(bootState);

        // Seed core1 with the default pattern 0, exactly as before this feature.
        const state::UIState& defaults = state::getStateManager().getState();
        state::syncGateSet(defaults, 0);
        state::syncPitchSet(defaults, 0);
        state::syncVelocitySet(defaults, 0);
    }

    // Input last: events must not arrive before the views are registered.
    keyboard = std::make_unique<hardware::UsbKeyboard>();
    keyboard->initialize();

    printf("UI Controller initialized\n");
}

void UIController::onStateChanged(const state::UIState& newState)
{
    IView* newView = views[static_cast<size_t>(newState.currentView)];
    
    if (newView != activeView) {
        if (activeView != nullptr) {
            activeView->onExit();
        }
        activeView = newView;
        activeView->onEnter();
    }
    
    activeView->render(newState);
}

void UIController::drainSaveRequest()
{
    const state::UIState& state = state::getStateManager().getState();

    // Only act on a pending request, and never launch a second save while one
    // is already running (in-progress debounce, Req 1.4). Because save() below
    // is synchronous, saveInProgress is only ever observed true here if a
    // re-entrant call somehow occurred; it is otherwise cleared before update()
    // returns.
    if (!state.saveRequested || saveInProgress) {
        return;
    }

    // Snapshot the current UIState BEFORE clearing the flag so the save
    // operates on a consistent copy (Req 2.1, 2.2). The UI loop is
    // single-threaded, so this copy is never torn.
    state::UIState snapshot = state;

    // Clear saveRequested first (via a mutable copy + setState) so the same
    // request is not re-run on the next tick even if the save fails (Req 1.4).
    // We install the cleared flag now and update the banner after the save
    // completes.
    state::UIState next = state;
    next.saveRequested = false;
    state::getStateManager().setState(next);

    // Map the snapshot to the durable persistence config and perform the save.
    // save() is synchronous/blocking: it parks core1 and performs the flash
    // erase+program on this core, returning an outcome directly. We mark
    // saveInProgress around the call so any re-entrant Ctrl+S during the (brief)
    // window is ignored; since the call returns synchronously it is effectively
    // cleared immediately after (Req 1.4).
    saveInProgress = true;
    const uint32_t startMs = to_ms_since_boot(get_absolute_time());

    const persistence::PersistableConfig cfg =
        state::toPersistableConfig(snapshot);
    const persistence::SaveOutcome outcome = persistenceManager->save(cfg);

    const uint32_t endMs = to_ms_since_boot(get_absolute_time());
    saveInProgress = false;

    // Map the outcome to a banner. Any failure variant collapses to FAILURE
    // (Req 1.5, 1.6, 7.1, 7.2). As a defensive watchdog (Req 1.1, 7.5), if the
    // synchronous write somehow exceeded the completion bound, force FAILURE
    // even on a reported success.
    state::SaveBanner banner;
    if (outcome == persistence::SaveOutcome::SUCCESS &&
        (endMs - startMs) <= SAVE_COMPLETION_LIMIT_MS) {
        banner = state::SaveBanner::SUCCESS;
    } else {
        banner = state::SaveBanner::FAILURE;
    }

    // Write the banner into the UIState so task 7.3 renders it, and record the
    // timestamp so 7.3 can auto-dismiss it after 1-2 s (Req 7.3). Re-read the
    // current state (rather than reusing `next`) so we do not clobber any state
    // change a listener may have applied during the save.
    state::UIState afterSave = state::getStateManager().getState();
    afterSave.saveBanner = banner;
    state::getStateManager().setState(afterSave);

    bannerActive = true;
    bannerSetMs = to_ms_since_boot(get_absolute_time());
    bannerRendered = false;  // force updateSaveBanner() to repaint the matrix
}

void UIController::updateSaveBanner()
{
    const state::UIState& state = state::getStateManager().getState();

    // Latch step (handles BOTH the save-outcome banners already latched in
    // drainSaveRequest() AND the boot NOT_LOADED banner set by task 7.1, which
    // set UIState.saveBanner but not bannerActive/bannerSetMs). If a banner is
    // pending in the state but no timer is running yet, start the timer now so
    // the boot banner is shown and auto-dismissed just like a save banner
    // (Req 6.7).
    if (!bannerActive && state.saveBanner != state::SaveBanner::NONE) {
        bannerActive = true;
        bannerSetMs = to_ms_since_boot(get_absolute_time());
        bannerRendered = false;
    }

    if (!bannerActive) {
        return;
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const uint32_t elapsed = now - bannerSetMs;

    // Auto-dismiss once the banner has been visible long enough (Req 7.3). We
    // clear the banner flag, reset UIState.saveBanner to NONE, and re-issue the
    // current state via setState() so the subscribed onStateChanged() path
    // re-renders the active view's normal content over the matrix.
    if (elapsed >= BANNER_DISMISS_MS) {
        bannerActive = false;
        bannerRendered = false;

        // Stop any banner-driven onboard LED activity so the LED returns to a
        // neutral state before the active view takes over again.
        led->off();

        // The banner filled the ENTIRE matrix via fill(). The active view's
        // render() only draws its own sparse pixels and does NOT clear the
        // buffer (only onEnter() clears), so re-rendering the same view would
        // leave the banner fill as a background. Clear the matrix here so the
        // view repaints onto a clean slate. Without this the banner colour
        // persists until the next view change (which clears via onEnter()).
        ledMatrix->clear();

        state::UIState cleared = state;
        cleared.saveBanner = state::SaveBanner::NONE;
        state::getStateManager().setState(cleared);
        return;
    }

    // Render the banner exactly once per activation so we neither churn the
    // matrix every tick nor fight the active view. While the banner is active
    // it owns the matrix; on dismissal the setState() above restores the view.
    if (bannerRendered) {
        return;
    }

    switch (state.saveBanner) {
        case state::SaveBanner::SUCCESS:
            ledMatrix->fill(BANNER_COLOR_SUCCESS);
            led->on();
            break;
        case state::SaveBanner::FAILURE:
            ledMatrix->fill(BANNER_COLOR_FAILURE);
            led->blink(100, 100);
            break;
        case state::SaveBanner::NOT_LOADED:
            ledMatrix->fill(BANNER_COLOR_NOT_LOADED);
            led->blink(500, 500);
            break;
        case state::SaveBanner::NONE:
        default:
            // No banner content to draw; nothing owns the matrix this tick.
            return;
    }

    bannerRendered = true;
}

void UIController::update()
{
    keyboard->update();

    // Drain any pending Ctrl+S save request. The save() call is synchronous but
    // bounded by the flash write time, so keyboard/display updates simply
    // resume on the next tick (Req 7.4). Task 7.3 owns clearing the banner
    // after 1-2 s using bannerActive / bannerSetMs recorded here.
    drainSaveRequest();

    // Present / auto-dismiss any active save or boot banner on the LED matrix
    // (persist-settings, task 7.3). Runs after drainSaveRequest() so a
    // freshly-latched save banner is picked up this tick, and BEFORE
    // ledMatrix->update() so the fill()/clear() it issues is flushed to the
    // strip on this same tick. Non-blocking (Req 7.4).
    updateSaveBanner();

    led->update();
    ledMatrix->update();
}

} // namespace ui
