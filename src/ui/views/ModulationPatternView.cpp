#include "ModulationPatternView.h"

#include <cstdio>

#include "../../commands/command.h"
#include "../../sequencer/ParameterAddress.h"
#include "../Types.h"

namespace ui {

using sequencer::ParameterScope;
using sequencer::ParamId;
using sequencer::StepUnit;

namespace {

// The 3-character matrix labels (drawLabel takes exactly a 4-byte array:
// three characters plus a trailing byte).
constexpr char kPatternLabel[4] = {'P', 'a', 't', '\0'};
constexpr char kParamLabel[4] = {'P', 'a', 'r', '\0'};
constexpr char kScopeLabel[4] = {'S', 'c', 'p', '\0'};
constexpr char kScaleLabel[4] = {'S', 'c', 'l', '\0'};
constexpr char kBalanceLabel[4] = {'B', 'a', 'l', '\0'};
constexpr char kDurationLabel[4] = {'D', 'u', 'r', '\0'};
constexpr char kUnitLabel[4] = {'U', 'n', 't', '\0'};
constexpr char kOrderLabel[4] = {'O', 'r', 'd', '\0'};
constexpr char kNoneLabel[4] = {'N', 'o', 'n', '\0'};

// The display palette (sibling-view conventions).
constexpr uint32_t kLabelColor = 0xFFFFEE00;  // label / pattern cursor / bar unit
constexpr uint32_t kNumberColor = 0xFF0011EE; // the numeric readouts
constexpr uint32_t kDimColor = 0xFF003300;    // the pattern-list row / beat unit
constexpr uint32_t kErrorColor = 0xFFFF0000;  // rejection / "no target" page

// Euclidean modulo (the C++ % of a negative value is negative): maps any
// `value` into [0, modulus). Bounded O(1) arithmetic, no loops.
int wrapMod(int value, int modulus) {
    const int wrapped = value % modulus;
    return wrapped < 0 ? wrapped + modulus : wrapped;
}

// The re-pointed parameter when a scope switch would strand a mismatched
// parameter (the class comment's binding rule, kept from task 21): global
// keeps only the BPM, per-pattern falls back to the first per-pattern
// parameter (GateSteps).
ParamId rePointedParamId(ParameterScope scope) {
    return (scope == ParameterScope::Global) ? ParamId::GlobalBpm
                                             : static_cast<ParamId>(1);
}

}  // namespace

ModulationPatternView::ModulationPatternView(hardware::LedMatrix& ledMatrix)
    : ledMatrix_(ledMatrix) {}

// --- pattern list (FR7/FR11/FR16) ------------------------------------------

uint8_t ModulationPatternView::patternCount() {
    return kPatternCount;
}

ModulationPatternView::Mode ModulationPatternView::mode() const {
    return mode_;
}

bool ModulationPatternView::selectPatternIndex(uint8_t index) {
    if (index >= kPatternCount) return false;  // refused out of the pool
    patternCursor_ = index;
    return true;
}

void ModulationPatternView::cyclePatternIndex(int delta) {
    patternCursor_ = static_cast<uint8_t>(
        wrapMod(static_cast<int>(patternCursor_) + delta, kPatternCount));
}

uint8_t ModulationPatternView::selectedPatternIndex() const {
    return patternCursor_;
}

bool ModulationPatternView::enterTargetConfig(uint8_t patternIndex) {
    if (patternIndex >= kPatternCount) return false;
    patternCursor_ = patternIndex;
    configuredPattern_ = patternIndex;
    selectedTargetSlot_ = firstBoundTargetSlot(patternIndex);
    // The picker cursor follows the drilled target's binding ("Non" keeps
    // the factory BPM cursor).
    paramCursor_ = (selectedTargetSlot_ == kNoTarget)
                       ? static_cast<uint8_t>(ParamId::GlobalBpm)
                       : static_cast<uint8_t>(targets_[selectedTargetSlot_]
                                                  .address.paramId);
    field_ = Field::Param;
    mode_ = Mode::TargetConfig;
    lastActionRejected_ = false;
    return true;
}

void ModulationPatternView::enterTargetConfig() {
    (void)enterTargetConfig(patternCursor_);
}

void ModulationPatternView::exitTargetConfig() {
    mode_ = Mode::PatternList;
    lastActionRejected_ = false;
}

uint8_t ModulationPatternView::configuredPattern() const {
    return configuredPattern_;
}

uint8_t ModulationPatternView::boundTargetCount(uint8_t patternIndex) const {
    if (patternIndex >= kPatternCount) return 0;
    uint8_t count = 0;
    for (uint8_t slot = 0; slot < kMaxTargetSlots; ++slot) {
        if (slotBoundTo(slot, patternIndex)) ++count;
    }
    return count;  // bounded sweep over the fixed slot pool
}

// --- the return view ----------------------------------------------------------

void ModulationPatternView::setReturnView(state::ViewId view) {
    returnView_ = view;
}

state::ViewId ModulationPatternView::returnView() const {
    return returnView_;
}

// --- target slots (FR13) -------------------------------------------------------

uint8_t ModulationPatternView::targetSlotCount() const {
    return kMaxTargetSlots;
}

uint8_t ModulationPatternView::activeTargetCount() const {
    uint8_t count = 0;
    for (uint8_t slot = 0; slot < kMaxTargetSlots; ++slot) {
        if (targets_[slot].active) ++count;
    }
    return count;  // bounded sweep over the fixed slot pool
}

uint8_t ModulationPatternView::addTarget(uint8_t patternIndex) {
    if (patternIndex >= kPatternCount) return kNoTarget;  // programming error
    for (uint8_t slot = 0; slot < kMaxTargetSlots; ++slot) {
        if (!targets_[slot].active) {
            targets_[slot] = TargetState{};  // factory configuration
            targets_[slot].active = true;
            targets_[slot].patternIndex = patternIndex;
            selectedTargetSlot_ = slot;
            paramCursor_ = static_cast<uint8_t>(ParamId::GlobalBpm);
            lastActionRejected_ = false;
            // The claim itself crosses to the sequencer core: the slot's
            // factory mirror (scope, deliberately invalid factory address,
            // scale 64, beat/1, forward) arrives on the other side by
            // construction — the two sides agree on the factory shape.
            commands::sendModTargetAddCommand(patternIndex);
            return slot;
        }
    }
    lastActionRejected_ = true;  // full pool: surfaced, never a block
    return kNoTarget;
}

bool ModulationPatternView::selectTargetSlot(uint8_t slot) {
    if (slot >= kMaxTargetSlots || !targets_[slot].active) return false;
    selectedTargetSlot_ = slot;
    paramCursor_ = static_cast<uint8_t>(targets_[slot].address.paramId);
    lastActionRejected_ = false;
    return true;
}

void ModulationPatternView::cycleTarget(int delta) {
    if (boundTargetCount(configuredPattern_) == 0) return;  // nothing to cycle
    int cursor;
    if (slotBoundTo(selectedTargetSlot_, configuredPattern_)) {
        cursor = selectedTargetSlot_;
    } else {
        cursor = -1;  // walk from before the first / after the last slot
    }
    for (int hops = 0; hops < kMaxTargetSlots; ++hops) {  // bounded sweep
        cursor += (delta >= 0) ? 1 : -1;
        cursor = wrapMod(cursor, kMaxTargetSlots);
        const uint8_t slot = static_cast<uint8_t>(cursor);
        if (slotBoundTo(slot, configuredPattern_)) {
            (void)selectTargetSlot(slot);
            return;
        }
    }
}

uint8_t ModulationPatternView::selectedTargetSlot() const {
    return selectedTargetSlot_;
}

bool ModulationPatternView::hasSelectedTarget() const {
    return selectedTargetSlot_ < kMaxTargetSlots &&
           targets_[selectedTargetSlot_].active;
}

bool ModulationPatternView::lastActionRejected() const {
    return lastActionRejected_;
}

// --- the parameter picker (FR12) ------------------------------------------------

uint8_t ModulationPatternView::paramCount() const {
    return sequencer::kParamIdCount;
}

bool ModulationPatternView::paramAt(uint8_t index, ParamOption& out) const {
    if (index >= sequencer::kParamIdCount) return false;
    const sequencer::ParamInfo& info = sequencer::kParamInfos[index];
    out.name = info.name;
    out.minValue = info.minValue;
    out.maxValue = info.maxValue;
    out.scope = info.scope;
    return true;  // the compile-time table, metadata verbatim
}

uint8_t ModulationPatternView::selectedParam() const {
    return paramCursor_;
}

bool ModulationPatternView::selectParam(uint8_t index) {
    if (index >= sequencer::kParamIdCount) return false;
    if (!hasSelectedTarget()) return false;
    ParamOption option;
    if (!paramAt(index, option)) return false;
    TargetState& target = selectedTargetWritable();
    // The local mirror takes the full address: the table's scope plus the
    // mirrored pattern slot for per-pattern bindings (the sequencer's
    // ParamId handler adopts the table's scope by construction, so the
    // ParamId post carries a valid binding on its own).
    target.address.paramId = static_cast<ParamId>(index);
    target.address.scope = option.scope;
    if (option.scope == ParameterScope::Pattern) {
        target.address.patternIndex = addressPatternSlot_;
        commands::sendModTargetSetParamCommand(
            selectedTargetSlot_, commands::ModTargetField::AddressPatternSlot,
            addressPatternSlot_);
    }
    paramCursor_ = index;
    commands::sendModTargetSetParamCommand(
        selectedTargetSlot_, commands::ModTargetField::ParamId, index);
    lastActionRejected_ = false;
    return true;
}

void ModulationPatternView::cycleParam(int delta) {
    paramCursor_ = static_cast<uint8_t>(
        wrapMod(static_cast<int>(paramCursor_) + delta, paramCount()));
}

// --- scope (FR13) ------------------------------------------------------------------

sequencer::ParameterScope ModulationPatternView::targetScope() const {
    return selectedTarget().address.scope;
}

bool ModulationPatternView::setTargetScope(ParameterScope scope) {
    if (scope != ParameterScope::Pattern && scope != ParameterScope::Global)
        return false;  // a foreign ordinal byte
    if (!hasSelectedTarget()) return false;
    const uint8_t slot = selectedTargetSlot_;
    TargetState& target = selectedTargetWritable();
    const sequencer::ParamInfo* info = sequencer::paramInfoOf(
        static_cast<uint16_t>(target.address.paramId));
    if (info != nullptr && info->scope != scope) {
        // The binding rule: re-point a scope-mismatched parameter so the
        // address stays VALID (the sequencer's Scope handler enforces the
        // same rule, so both sides re-point identically).
        const ParamId rePointed = rePointedParamId(scope);
        target.address.paramId = rePointed;
        paramCursor_ = static_cast<uint8_t>(rePointed);
        commands::sendModTargetSetParamCommand(
            slot, commands::ModTargetField::ParamId,
            static_cast<uint8_t>(rePointed));
    }
    target.address.scope = scope;
    if (scope == ParameterScope::Pattern) {
        target.address.patternIndex = addressPatternSlot_;
        commands::sendModTargetSetParamCommand(
            slot, commands::ModTargetField::AddressPatternSlot,
            addressPatternSlot_);
    }
    commands::sendModTargetSetParamCommand(
        slot, commands::ModTargetField::Scope, static_cast<uint8_t>(scope));
    lastActionRejected_ = false;
    return true;
}

void ModulationPatternView::toggleTargetScope() {
    (void)setTargetScope(
        (targetScope() == ParameterScope::Pattern) ? ParameterScope::Global
                                                    : ParameterScope::Pattern);
}

bool ModulationPatternView::selectAddressPatternSlot(uint8_t slot) {
    if (slot >= common::PATTERN_CAPACITY) return false;
    addressPatternSlot_ = slot;
    return true;  // mirrored now, posted with the next per-pattern write
}

uint8_t ModulationPatternView::addressPatternSlot() const {
    return addressPatternSlot_;
}

// --- FR14 per-target config --------------------------------------------------------

uint8_t ModulationPatternView::targetScaling() const {
    return selectedTarget().scaleFactor;
}

bool ModulationPatternView::setTargetScaling(int value) {
    if (!hasSelectedTarget()) return false;
    const uint8_t clamped =
        static_cast<uint8_t>(clampInt(value, 0, kMaxStepValue));
    selectedTargetWritable().scaleFactor = clamped;
    commands::sendModTargetSetParamCommand(
        selectedTargetSlot_, commands::ModTargetField::ScaleFactor, clamped);
    lastActionRejected_ = false;
    return true;
}

void ModulationPatternView::adjustTargetScaling(int delta) {
    (void)setTargetScaling(static_cast<int>(targetScaling()) + delta);
}

int8_t ModulationPatternView::targetBalance() const {
    return selectedTarget().balance;
}

bool ModulationPatternView::setTargetBalance(int value) {
    if (!hasSelectedTarget()) return false;
    const int clamped = clampInt(value, kMinBalance, kMaxBalance);
    const int8_t signedValue = static_cast<int8_t>(clamped);
    selectedTargetWritable().balance = signedValue;
    // Signed byte: pack the negative balance as its two's-complement byte
    // (command.h); the sequencer side signs it again.
    commands::sendModTargetSetParamCommand(
        selectedTargetSlot_, commands::ModTargetField::Balance,
        static_cast<uint8_t>(signedValue));
    lastActionRejected_ = false;
    return true;
}

void ModulationPatternView::adjustTargetBalance(int delta) {
    (void)setTargetBalance(static_cast<int>(targetBalance()) + delta);
}

sequencer::StepDuration ModulationPatternView::targetStepDuration() const {
    return selectedTarget().stepDuration;
}

bool ModulationPatternView::setStepDurationLength(int length) {
    if (!hasSelectedTarget()) return false;
    const uint16_t clamped =
        static_cast<uint16_t>(clampInt(length, 1, kMaxDurationLength));
    selectedTargetWritable().stepDuration.length = clamped;
    commands::sendModTargetSetParamCommand(
        selectedTargetSlot_, commands::ModTargetField::DurationLength,
        static_cast<uint8_t>(clamped));
    lastActionRejected_ = false;
    return true;
}

bool ModulationPatternView::setStepDurationUnit(StepUnit unit) {
    if (unit != StepUnit::Beat && unit != StepUnit::Bar) return false;
    if (!hasSelectedTarget()) return false;
    selectedTargetWritable().stepDuration.unit = unit;
    commands::sendModTargetSetParamCommand(
        selectedTargetSlot_, commands::ModTargetField::DurationUnit,
        static_cast<uint8_t>(unit));
    lastActionRejected_ = false;
    return true;
}

void ModulationPatternView::toggleStepDurationUnit() {
    const StepUnit current = selectedTarget().stepDuration.unit;
    (void)setStepDurationUnit((current == StepUnit::Beat) ? StepUnit::Bar
                                                          : StepUnit::Beat);
}

sequencer::ModulationOrder ModulationPatternView::targetOrder() const {
    return selectedTarget().order;
}

bool ModulationPatternView::setTargetOrder(sequencer::ModulationOrder order) {
    using sequencer::ModulationOrder;
    if (order != ModulationOrder::Forward && order != ModulationOrder::Reverse &&
        order != ModulationOrder::PingPong && order != ModulationOrder::Random)
        return false;  // a foreign ordinal byte
    if (!hasSelectedTarget()) return false;
    selectedTargetWritable().order = order;
    commands::sendModTargetSetParamCommand(
        selectedTargetSlot_, commands::ModTargetField::Order,
        static_cast<uint8_t>(order));
    lastActionRejected_ = false;
    return true;
}

void ModulationPatternView::cycleTargetOrder(int delta) {
    using sequencer::ModulationOrder;
    const int next = wrapMod(
        static_cast<int>(targetOrder()) + (delta >= 0 ? 1 : -1),
        4);  // Forward, Reverse, PingPong, Random
    (void)setTargetOrder(static_cast<ModulationOrder>(next));
}

// --- field selection ------------------------------------------------------------------

bool ModulationPatternView::selectField(Field field) {
    if (field != Field::Param && field != Field::Scope && field != Field::Scale &&
        field != Field::Balance && field != Field::Duration &&
        field != Field::Unit && field != Field::Order)
        return false;  // a foreign enum byte
    field_ = field;
    return true;
}

ModulationPatternView::Field ModulationPatternView::selectedField() const {
    return field_;
}

void ModulationPatternView::cycleField(int delta) {
    field_ = static_cast<Field>(
        wrapMod(static_cast<int>(field_) + delta, kFieldCount));
}

// --- IView -----------------------------------------------------------------------------

void ModulationPatternView::onEnter() {
    std::printf("Entering Modulation Pattern View\n");
    ledMatrix_.clear();
    // FR16: entry always shows the pattern list.
    mode_ = Mode::PatternList;
}

state::UIState ModulationPatternView::handleEvent(const state::UIState& state,
                                                  const events::Event& event) {
    // ESCAPE backs out two levels (FR16): one press from the target
    // configuration returns to the pattern list (a view-local mode change,
    // the UIState itself unchanged); one press from the pattern list hands
    // control back to the previous global view (the wiring layer sets it
    // on entry; PATTERNS by default). Held repeats and releases are
    // ignored, matching the sibling views.
    if (event.type == events::EventType::KEY_PRESSED &&
        event.data.key.id == KeyId::ESCAPE) {
        if (mode_ == Mode::TargetConfig) {
            exitTargetConfig();
            return state;
        }
        state::UIState next = state;
        next.currentView = returnView_;
        return next;
    }

    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;  // key releases change nothing
    }

    // Mirror the UI's selected sequencer pattern into per-pattern addresses
    // (a target bound while pattern 7 is selected addresses pattern 7).
    if (state.selectedPattern < common::PATTERN_CAPACITY) {
        addressPatternSlot_ = state.selectedPattern;
    }

    const bool coarse = (event.data.key.mods & mod::SHIFT) != 0;
    const int step = coarse ? kCoarseStep : 1;
    const KeyId key = event.data.key.id;
    switch (combo(key, event.data.key.mods & ~mod::SHIFT)) {
        case combo(KeyId::ENTER): {
            // List page: drill in (FR8). Config page: advance the edited
            // field (wrapping) — the claim moved to A (see the class
            // comment).
            if (mode_ == Mode::PatternList) {
                enterTargetConfig();
            } else {
                cycleField(1);
            }
            break;
        }
        case combo(KeyId::ESCAPE):
            // Held ESCAPE: ignored (only the press navigates, above).
            break;
        default: {
            if (mode_ == Mode::PatternList) {
                switch (combo(key, event.data.key.mods & ~mod::SHIFT)) {
                    case combo(KeyId::UP):
                    case combo(KeyId::LEFT):
                        cyclePatternIndex(-1);  // previous, wrapping
                        break;
                    case combo(KeyId::DOWN):
                    case combo(KeyId::RIGHT):
                        cyclePatternIndex(1);   // next, wrapping
                        break;
                    default:
                        break;  // unmapped key: a no-op
                }
            } else {
                switch (combo(key, event.data.key.mods & ~mod::SHIFT)) {
                    case combo(KeyId::TAB):
                    case combo(KeyId::RIGHT):
                        cycleTarget(1);   // next bound target, wrapping
                        break;
                    case combo(KeyId::LEFT):
                        cycleTarget(-1);  // previous bound target, wrapping
                        break;
                    case combo(KeyId::UP):
                        adjustSelectedField(step);
                        break;
                    case combo(KeyId::DOWN):
                        adjustSelectedField(-step);
                        break;
                    case combo(KeyId::A):
                        // Claim a new target bound to the drilled pattern
                        // (FR13): a full pool is surfaced by the render
                        // path, never a block.
                        (void)addTarget(configuredPattern_);
                        break;
                    default:
                        break;  // unmapped key: a no-op
                }
            }
            break;
        }
    }
    return state;  // view-local state only; the UIState is unchanged
}

void ModulationPatternView::render(const state::UIState& state) {
    (void)state;
    ledMatrix_.clear();
    if (mode_ == Mode::PatternList) {
        ledMatrix_.drawLabel(kPatternLabel, kLabelColor);
        // The list: one dim pixel per global pattern, the cursor bright.
        for (uint8_t i = 0; i < kPatternCount; ++i) {
            ledMatrix_.setPixel(i, 0, (i == patternCursor_) ? kLabelColor
                                                            : kDimColor);
        }
        ledMatrix_.drawNumber(patternCursor_, kNumberColor);
        return;
    }
    if (!hasSelectedTarget()) {
        // A drilled pattern with no target slots bound ("Non"): the config
        // page reads as the factory until a target is claimed.
        ledMatrix_.drawLabel(kNoneLabel, kErrorColor);
        if (lastActionRejected_) {
            ledMatrix_.setPixel(15, 15, kErrorColor);
        }
        return;
    }
    switch (field_) {
        case Field::Param:
            ledMatrix_.drawLabel(kParamLabel, kLabelColor);
            ledMatrix_.drawNumber(selectedParam(), kNumberColor);
            break;
        case Field::Scope:
            ledMatrix_.drawLabel(kScopeLabel, kLabelColor);
            ledMatrix_.drawNumber(
                static_cast<int>(targetScope() == ParameterScope::Global),
                kNumberColor);
            break;
        case Field::Scale:
            ledMatrix_.drawLabel(kScaleLabel, kLabelColor);
            ledMatrix_.drawNumber(targetScaling(), kNumberColor);
            break;
        case Field::Balance:
            ledMatrix_.drawLabel(kBalanceLabel, kLabelColor);
            ledMatrix_.drawNumber(targetBalance(), kNumberColor);
            break;
        case Field::Duration:
            ledMatrix_.drawLabel(kDurationLabel, kLabelColor);
            ledMatrix_.drawNumber(targetStepDuration().length, kNumberColor);
            break;
        case Field::Unit:
            ledMatrix_.drawLabel(kUnitLabel, kLabelColor);
            ledMatrix_.drawNumber(
                static_cast<int>(targetStepDuration().unit == StepUnit::Bar),
                kNumberColor);
            break;
        case Field::Order:
            ledMatrix_.drawLabel(kOrderLabel, kLabelColor);
            ledMatrix_.drawNumber(static_cast<int>(targetOrder()), kNumberColor);
            break;
        default:
            break;
    }
    if (lastActionRejected_) {
        ledMatrix_.setPixel(15, 15, kErrorColor);  // a surfaced refusal
    }
}

// --- private helpers ---------------------------------------------------------------

const ModulationPatternView::TargetState& ModulationPatternView::selectedTarget()
    const {
    static const TargetState kNullTarget{};  // total reads (factory config)
    if (!hasSelectedTarget()) return kNullTarget;
    return targets_[selectedTargetSlot_];
}

ModulationPatternView::TargetState& ModulationPatternView::selectedTargetWritable() {
    // Writers must guard with hasSelectedTarget() first; this returns a
    // per-view scratch slot so a stray write can never alias another slot.
    static TargetState scratch{};
    if (!hasSelectedTarget()) return scratch;
    return targets_[selectedTargetSlot_];
}

uint8_t ModulationPatternView::firstBoundTargetSlot(uint8_t patternIndex) const {
    for (uint8_t slot = 0; slot < kMaxTargetSlots; ++slot) {
        if (slotBoundTo(slot, patternIndex)) return slot;
    }
    return kNoTarget;  // bounded sweep over the fixed slot pool
}

bool ModulationPatternView::slotBoundTo(uint8_t slot, uint8_t patternIndex) const {
    return slot < kMaxTargetSlots && targets_[slot].active &&
           targets_[slot].patternIndex == patternIndex;
}

int ModulationPatternView::clampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void ModulationPatternView::adjustSelectedField(int step) {
    switch (field_) {
        case Field::Param:
            // Move the picker cursor and COMMIT the new binding (a Param
            // field edit posts the address, not just the cursor).
            cycleParam(step);
            (void)selectParam(paramCursor_);
            break;
        case Field::Scope:
            // Either direction toggles (the two-option field).
            toggleTargetScope();
            break;
        case Field::Scale:
            adjustTargetScaling(step);
            break;
        case Field::Balance:
            adjustTargetBalance(step);
            break;
        case Field::Duration:
            (void)setStepDurationLength(
                static_cast<int>(targetStepDuration().length) + step);
            break;
        case Field::Unit:
            toggleStepDurationUnit();
            break;
        case Field::Order:
            cycleTargetOrder(step >= 0 ? 1 : -1);
            break;
        default:
            break;
    }
}

}  // namespace ui
