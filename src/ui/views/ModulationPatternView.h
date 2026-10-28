#pragma once

#include <cstdint>

#include "IView.h"
#include "../hardware/LedMatrix.h"
#include "../../sequencer/ModulationRegistry.h"
#include "../../sequencer/ModulationTarget.h"
#include "../../sequencer/ParameterAddress.h"

namespace ui {

// Modulation pattern list + target configuration view
// (ui-integration T7, FR7-FR14, FR16).
//
// The operator's front end to the modulation layer (generative-engine-
// expansion tasks 15-17): one view, two view-local PAGES.
//
//   Pattern list (FR7/FR11/FR16): which of the sequencer's fixed pool of
//   GLOBAL ModulationPatterns a target will be bound to. The list shows
//   every pattern of the pool on entry; the cursor moves freely (wrap)
//   and ENTER drills into the cursor pattern's TARGET CONFIGURATION.
//
//   Target configuration (FR12-FR14): the drilled pattern's first bound
//   target (FR8) — the target slot a ModulationTarget rides in, the
//   parameter it modulates (picked from the FULL task-16 table, including
//   the global BPM), the scope (FR13: per-pattern by default, global the
//   option) and the FR14 scaling / balance / step duration / playing
//   order.
//
// ui-integration T7: the view holds NO ModulationRegistry and NO
// ParameterBus — the core0/core1 boundary is the command queue ONLY. It
// keeps its OWN local mirror of the fixed target pool (factory-claimed
// targets, every subsequent edit) and posts every action as a command:
//   MOD_TARGET_ADD           - claim a target slot bound to a pattern
//   MOD_TARGET_SET_PARAM     - write one field of one target slot
// The sequencer core resolves those commands against ITS registry (the
// storage the tick-path ModulationApplier reads), so no data structure
// ever crosses the cores and the two sides only agree on the command
// vocabulary (command.h), the slot/pattern indices and the compile-time
// parameter table (ParameterAddress.h) — the same split every other
// ui-integration view (T4-T6) already uses. ModulationRegistry.h is
// included for its compile-time capacity constants only; the view never
// touches the registry object. The view NEVER writes a ParameterBus
// value: overlays stay exclusive to the per-tick applier (Milestone 2 /
// AC3 data integrity untouched by the UI).
//
// Scope binding rule (kept from task 21, now mirrored on BOTH sides): a
// ParameterAddress must carry the scope its parameter is defined under
// (BPM is global-only, everything else per-pattern). Selecting a
// parameter posts the table's scope alongside it (the sequencer's ParamId
// handler adopts it, so an explicitly selected parameter is always
// VALIDLY addressed by construction); switching the scope re-points a
// scope-mismatched parameter (global keeps only the BPM, per-pattern
// falls back to the first per-pattern parameter) instead of leaving a
// dead address — the sequencer's Scope handler enforces the same rule.
// A freshly claimed target keeps its deliberately INVALID factory address
// until the performer binds one, so an unconfigured target modulates
// nothing and default behavior stays stable.
//
// Selection state is view-local by design (UIState carries no modulation
// fields), so handleEvent returns the UIState unchanged — the ONLY
// exception is ESCAPE on the pattern list, which hands control back to
// the previous global view (the wiring layer sets it on entry; PATTERNS
// by default). handleEvent mirrors state.selectedPattern into the
// per-pattern address slot so a target bound while pattern 7 is selected
// addresses pattern 7.
//
// Key mapping (documented, follows the sibling view conventions):
//   pattern list:
//     UP/DOWN/LEFT/RIGHT - move the pattern cursor (wrapping)
//     ENTER              - drill into the cursor pattern's target
//                          configuration (selects its FIRST bound
//                          target; "Non" when the pattern has none)
//     ESCAPE             - return to the previous global view
//   target configuration:
//     TAB / LEFT / RIGHT - cycle the drilled pattern's bound target slots
//                          (wrapping; other patterns' targets are never
//                          reachable from this page)
//     UP / DOWN          - adjust the edited field (SHIFT: coarse step 10):
//                            Param:    previous/next table entry (wrap)
//                                      + COMMIT the new binding
//                            Scope:    toggle per-pattern <-> global
//                            Scale:    +/- step, clamped 0-127
//                            Balance:  +/- step, clamped -127..127
//                            Duration: +/- step length, clamped 1-16
//                            Unit:     toggle beat/bar
//                            Order:    forward/reverse/ping-pong/random
//     ENTER              - advance the edited field (wrapping)
//     A                  - claim a new target slot bound to the drilled
//                          pattern (a full registry is surfaced as a
//                          rejected action, never a block)
//     ESCAPE             - back out to the pattern list
//
// The command protocol also carries a PatternIndex field (re-binding a
// target to another pattern), but the drill-based flow derives the
// driving pattern from the drilled list row, so this view never posts
// that field; it stays available to future UIs.
//
// Global constraints honored here: no heap allocation, no unbounded loops
// (every operation is a bounded sweep over the fixed pattern/target/
// parameter tables), fixed-capacity storage (scalar members only), never
// a block. The view never touches the tick path.
class ModulationPatternView : public IView {
public:
    // The fixed pools both cores agree on (the sequencer's compile-time
    // capacities are part of the cross-core command contract: the payload
    // addresses patterns/target slots BY INDEX).
    static constexpr uint8_t kPatternCount =
        static_cast<uint8_t>(sequencer::ModulationRegistry::MAX_PATTERNS);
    static constexpr uint8_t kMaxTargetSlots =
        static_cast<uint8_t>(sequencer::ModulationRegistry::MAX_TARGETS);
    // FR14 scaling factor bound (0-127; the factory default is 64).
    static constexpr int kMaxStepValue = 127;
    // FR14 balance bounds (int8_t storage on the target).
    static constexpr int kMaxBalance = 127;
    static constexpr int kMinBalance = -127;
    // The documented UI bound for a step duration's unit count (the target
    // stores uint16_t; the UI keeps it a sane small count).
    static constexpr int kMaxDurationLength = 16;
    // The coarse (SHIFT) adjustment step, matching the sibling views.
    static constexpr int kCoarseStep = 10;
    // Sentinel: no target slot selected.
    static constexpr uint8_t kNoTarget = 0xFF;

    // The view's two pages.
    enum class Mode : uint8_t {
        PatternList = 0,   // entry page: the global pattern list (FR16)
        TargetConfig = 1,  // the drilled pattern's target configuration
    };

    // The fields UP/DOWN adjust / ENTER cycles (see the class comment).
    enum class Field : uint8_t {
        Param = 0,    // the target parameter picker (full task-16 table)
        Scope = 1,    // FR13: per-pattern (default) / global
        Scale = 2,    // FR14 scaling factor (0-127)
        Balance = 3,  // FR14 balance value
        Duration = 4, // FR14 step duration length (1-16)
        Unit = 5,     // FR14 step duration unit (beat/bar)
        Order = 6,    // FR11 playing order
    };
    static constexpr uint8_t kFieldCount = 7;

    // One pickable parameter, mirroring the task-16 table entry the
    // sequencer core resolves for that ParamId (the view reads the
    // compile-time kParamInfos table — never a live ParameterBus).
    struct ParamOption {
        const char* name = "";
        int16_t minValue = 0;
        int16_t maxValue = 0;
        sequencer::ParameterScope scope = sequencer::ParameterScope::Pattern;
    };

    // The view's local mirror of one ModulationTarget slot. The shape and
    // the factory defaults match the sequencer side exactly (the two
    // sides stay in sync through the command stream, not shared state).
    struct TargetState {
        bool active = false;  // claimed by an ADD this session
        uint8_t patternIndex = 0;  // the driving global modulation pattern
        sequencer::ParameterAddress address{};  // deliberately invalid factory
        uint8_t scaleFactor = 64;
        int8_t balance = 0;
        sequencer::StepDuration stepDuration{};
        sequencer::ModulationOrder order = sequencer::ModulationOrder::Forward;
    };

    explicit ModulationPatternView(hardware::LedMatrix& ledMatrix);

    // --- pattern list (FR7/FR11/FR16) ---------------------------------------
    // The fixed global pattern pool (the sequencer's MAX_PATTERNS).
    static uint8_t patternCount();
    Mode mode() const;
    bool selectPatternIndex(uint8_t index);  // refused out of the pool
    void cyclePatternIndex(int delta);       // wraps
    uint8_t selectedPatternIndex() const;
    // Drill into `patternIndex`'s target configuration (FR8/FR16):
    // selects the pattern's FIRST bound target, or none when the pattern
    // has none ("Non"). The field cursor restarts at Param.
    bool enterTargetConfig(uint8_t patternIndex);  // refused out of the pool
    void enterTargetConfig();                      // the cursor pattern
    // Back out to the pattern list (the ESCAPE twin from the config page).
    void exitTargetConfig();
    // Which pattern's bound targets the config page edits (the last
    // drilled pattern; meaningful in TargetConfig mode).
    uint8_t configuredPattern() const;
    // How many of the pool's target slots are bound to `patternIndex`
    // (refused out of the pool: 0).
    uint8_t boundTargetCount(uint8_t patternIndex) const;

    // --- the return view (ESCAPE from the pattern list) ----------------------
    // The global view ESCAPE hands control back to (the wiring layer sets
    // it on entry; PATTERNS by default — the sibling convention).
    void setReturnView(state::ViewId view);
    state::ViewId returnView() const;

    // --- target slots (FR13) --------------------------------------------------
    // The fixed slot pool (the sequencer's MAX_TARGETS).
    uint8_t targetSlotCount() const;
    uint8_t activeTargetCount() const;
    // Claim a free slot bound to `patternIndex` (factory configuration,
    // selected, returned); post MOD_TARGET_ADD. kNoTarget + a rejected
    // latch when the pool is full — never a block, never a growth. An out-
    // of-pool pattern is refused without a post.
    uint8_t addTarget(uint8_t patternIndex);
    // Select an ACTIVE slot (an unclaimed one is refused); the picker
    // cursor re-points to the target's bound parameter.
    bool selectTargetSlot(uint8_t slot);
    // Move to the previous / next slot BOUND TO THE DRILLED pattern
    // (wrapping; other patterns' targets are never reachable; a no-op
    // when the pattern has none).
    void cycleTarget(int delta);
    uint8_t selectedTargetSlot() const;  // kNoTarget when none
    bool hasSelectedTarget() const;
    // True when the most recent action was rejected (a full target pool);
    // the render path surfaces it.
    bool lastActionRejected() const;

    // --- the parameter picker (FR12) ------------------------------------------
    // The picker lists EVERY task-16 table parameter (including the global
    // BPM), metadata verbatim.
    uint8_t paramCount() const;
    bool paramAt(uint8_t index, ParamOption& out) const;
    // The picker cursor (a table index; re-pointed to the selected
    // target's bound parameter by selectTargetSlot / addTarget).
    uint8_t selectedParam() const;
    // Bind the index-th table parameter to the selected target: writes the
    // local mirror's address (paramId + the table's scope + the pattern
    // slot for per-pattern scope) and posts the ParamId (the sequencer
    // adopts the table's scope by construction) plus the AddressPatternSlot
    // for per-pattern bindings. Refused out of range / without a selected
    // target (nothing posted).
    bool selectParam(uint8_t index);
    // Move the picker cursor ONLY (no commit, nothing posted): the commit
    // rides UP/DOWN on the Param field (see the class comment).
    void cycleParam(int delta);  // wraps over the whole table

    // --- scope (FR13) -----------------------------------------------------------
    // The selected target's address scope (the factory Pattern default
    // through the null fallback when nothing is selected). Switching
    // re-points a scope-mismatched parameter (see the class comment) and
    // posts the re-pointed ParamId, the Scope and the AddressPatternSlot
    // for per-pattern bindings.
    sequencer::ParameterScope targetScope() const;
    bool setTargetScope(sequencer::ParameterScope scope);
    void toggleTargetScope();
    // The sequencer pattern slot written into per-pattern addresses
    // (mirrors the UI's selected pattern; 0 by default; refused past the
    // capacity — NOT posted until the next per-pattern binding/scope
    // write).
    bool selectAddressPatternSlot(uint8_t slot);
    uint8_t addressPatternSlot() const;

    // --- FR14 per-target config -------------------------------------------------
    // Each setter validates / clamps, writes the LOCAL mirror and posts
    // one MOD_TARGET_SET_PARAM word; refused without a selected target
    // (nothing posted).
    uint8_t targetScaling() const;
    bool setTargetScaling(int value);  // clamped 0-127
    void adjustTargetScaling(int delta);
    int8_t targetBalance() const;
    bool setTargetBalance(int value);  // clamped -127..127
    void adjustTargetBalance(int delta);
    sequencer::StepDuration targetStepDuration() const;
    bool setStepDurationLength(int length);  // clamped 1-16, keeps the unit
    bool setStepDurationUnit(sequencer::StepUnit unit);  // foreign refused
    void toggleStepDurationUnit();
    sequencer::ModulationOrder targetOrder() const;
    bool setTargetOrder(sequencer::ModulationOrder order);  // foreign refused
    void cycleTargetOrder(int delta);  // wraps over the four orders

    // --- field selection ---------------------------------------------------------
    bool selectField(Field field);  // a foreign enum byte is refused
    Field selectedField() const;
    void cycleField(int delta);     // wraps

    // --- IView -------------------------------------------------------------------
    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state,
                               const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    // The selected target (bounds-checked, so nullptr never exists): reads
    // are total through a factory-configured fallback; WRITERS must check
    // hasSelectedTarget() first so an edit can never land in the fallback.
    const TargetState& selectedTarget() const;
    TargetState& selectedTargetWritable();
    // The lowest slot bound to `patternIndex` (kNoTarget when none).
    uint8_t firstBoundTargetSlot(uint8_t patternIndex) const;
    // True when `slot` is an active target bound to `patternIndex`.
    bool slotBoundTo(uint8_t slot, uint8_t patternIndex) const;
    // Clamp into [lo, hi] (lo <= hi).
    static int clampInt(int value, int lo, int hi);
    // Adjust the currently selected field by `step` (the key front end).
    void adjustSelectedField(int step);

    hardware::LedMatrix& ledMatrix_;
    state::ViewId returnView_ = state::ViewId::PATTERNS;  // ESCAPE target

    // View-local selection state (fixed-size scalars only). The target
    // pool mirror is the view's ONLY knowledge of the configuration: it
    // starts empty every session and fills as the performer claims and
    // edits targets, staying in sync with the sequencer side through the
    // posted command stream.
    Mode mode_ = Mode::PatternList;
    uint8_t patternCursor_ = 0;       // the list page's cursor pattern
    uint8_t configuredPattern_ = 0;   // the drilled pattern (config page)
    uint8_t selectedTargetSlot_ = kNoTarget;
    Field field_ = Field::Param;
    uint8_t paramCursor_ = 0;         // picker cursor (table index)
    uint8_t addressPatternSlot_ = 0;  // mirrors the UI's selected pattern
    bool lastActionRejected_ = false;
    TargetState targets_[kMaxTargetSlots] = {};
};

}  // namespace ui
