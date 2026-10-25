// Modulation pattern editor + target configuration view
// (generative-engine-expansion task 21, FR11-FR14). See
// ModulationPatternView.h for the design; this translation unit holds the
// registry write-through logic and the key-event front end.

#include "ModulationPatternView.h"

#include "../Types.h"

#include <cstdio>

namespace ui {

namespace {

using sequencer::ModulationOrder;
using sequencer::ModulationPattern;
using sequencer::ModulationRegistry;
using sequencer::ParameterScope;
using sequencer::ParamId;
using sequencer::StepUnit;

// The 3-character matrix labels (drawLabel takes exactly a 4-byte array:
// three characters plus a trailing byte).
constexpr char kPatternLabel[4] = {'P', 'a', 't', '\0'};
constexpr char kLengthLabel[4] = {'L', 'e', 'n', '\0'};
constexpr char kStepLabel[4] = {'S', 't', 'p', '\0'};
constexpr char kTargetLabel[4] = {'T', 'g', 't', '\0'};
constexpr char kBindLabel[4] = {'B', 'n', 'd', '\0'};
constexpr char kParamLabel[4] = {'P', 'a', 'r', '\0'};
constexpr char kScopeLabel[4] = {'S', 'c', 'p', '\0'};
constexpr char kScaleLabel[4] = {'S', 'c', 'l', '\0'};
constexpr char kBalanceLabel[4] = {'B', 'a', 'l', '\0'};
constexpr char kDurationLabel[4] = {'D', 'u', 'r', '\0'};
constexpr char kOrderLabel[4] = {'O', 'r', 'd', '\0'};
constexpr char kNoneLabel[4] = {'N', 'o', 'n', '\0'};

// Euclidean modulo (the C++ % of a negative value is negative): maps any
// `value` into [0, modulus). Bounded O(1) arithmetic, no loops.
int wrapMod(int value, int modulus) {
    const int wrapped = value % modulus;
    return wrapped < 0 ? wrapped + modulus : wrapped;
}

// Clamp into the FR11 step value range 0-127.
uint8_t clampStepValue(int value) {
    if (value < 0) return 0;
    if (value > ModulationPatternView::kMaxStepValue) {
        return static_cast<uint8_t>(ModulationPatternView::kMaxStepValue);
    }
    return static_cast<uint8_t>(value);
}

// Clamp into the FR11 length range 1-16.
int clampLength(int value) {
    if (value < ModulationPatternView::kMinLength) {
        return ModulationPatternView::kMinLength;
    }
    if (value > ModulationPatternView::kMaxLength) {
        return ModulationPatternView::kMaxLength;
    }
    return value;
}

}  // namespace

ModulationPatternView::ModulationPatternView(
    sequencer::ModulationRegistry& registry, sequencer::ParameterBus& bus,
    hardware::LedMatrix& ledMatrix)
    : registry_(registry), bus_(bus), ledMatrix_(ledMatrix) {
    // Enter with the first active target already selected (a registry with
    // targets configured elsewhere shows something usable); a fresh registry
    // simply has none until the performer claims one.
    if (registry_.targetActive(0)) selectedTargetSlot_ = 0;
}

// --- pattern editing (FR11) -----------------------------------------------------

bool ModulationPatternView::selectEditedPattern(uint8_t index) {
    if (index >= ModulationRegistry::MAX_PATTERNS) return false;  // refused
    editedPattern_ = index;
    clampStepCursor();
    return true;
}

void ModulationPatternView::cycleEditedPattern(int delta) {
    editedPattern_ = static_cast<uint8_t>(wrapMod(
        static_cast<int>(editedPattern_) + delta,
        static_cast<int>(ModulationRegistry::MAX_PATTERNS)));
    clampStepCursor();
}

uint8_t ModulationPatternView::editedPattern() const { return editedPattern_; }

void ModulationPatternView::setPatternLength(int length) {
    // Clamped, never refused (0 becomes 1, 17 becomes 16); the pattern's own
    // setLength accepts the clamped value. Steps beyond the new length are
    // PRESERVED (never cleared), so re-lengthening restores them.
    editedPatternRef().setLength(
        static_cast<uint8_t>(clampLength(length)));
    clampStepCursor();
}

void ModulationPatternView::adjustPatternLength(int delta) {
    setPatternLength(static_cast<int>(patternLength()) + delta);
}

uint8_t ModulationPatternView::patternLength() const {
    return editedPatternRef().length();
}

bool ModulationPatternView::stepEditable(uint8_t index) const {
    return index < patternLength();
}

bool ModulationPatternView::stepHighlighted(uint8_t index) const {
    // The render highlights exactly the editable steps: steps past the
    // current length stay dark.
    return stepEditable(index);
}

bool ModulationPatternView::selectStep(uint8_t index) {
    if (!stepEditable(index)) return false;  // the cursor never leaves the length
    stepCursor_ = index;
    return true;
}

bool ModulationPatternView::cycleStep(int delta) {
    if (!stepEditable(stepCursor_)) clampStepCursor();  // defensive; total
    const int length = static_cast<int>(patternLength());
    if (length <= 0) return false;
    int target = static_cast<int>(stepCursor_) + delta;
    if (target < 0) target = 0;
    if (target >= length) target = length - 1;
    if (target == static_cast<int>(stepCursor_)) {
        return false;  // already at the edge (the key front end leaves the field)
    }
    stepCursor_ = static_cast<uint8_t>(target);
    return true;
}

uint8_t ModulationPatternView::selectedStep() const { return stepCursor_; }

uint8_t ModulationPatternView::stepValue(uint8_t index) const {
    return editedPatternRef().stepAt(index);  // total (wraps modulo the length)
}

bool ModulationPatternView::setStepValue(uint8_t index, int value) {
    if (!stepEditable(index)) return false;  // only in-length steps are editable
    return editedPatternRef().setStep(index, clampStepValue(value));
}

void ModulationPatternView::adjustStepValue(int delta) {
    setStepValue(stepCursor_, static_cast<int>(stepValue(stepCursor_)) + delta);
}

// --- target slots (FR13) ----------------------------------------------------------

uint8_t ModulationPatternView::targetSlotCount() const {
    return static_cast<uint8_t>(ModulationRegistry::MAX_TARGETS);
}

uint8_t ModulationPatternView::activeTargetCount() const {
    return static_cast<uint8_t>(registry_.activeTargetCount());
}

uint8_t ModulationPatternView::addTarget() {
    const size_t slot = registry_.addTarget();  // factory configuration
    if (slot == ModulationRegistry::INVALID_INDEX) {
        // Fixed capacity: a full registry refuses — never a block, never a
        // growth. The rejection latches for the render path.
        lastActionRejected_ = true;
        return kNoTarget;
    }
    selectedTargetSlot_ = static_cast<uint8_t>(slot);
    // Keep the picker cursor in step with the freshly bound address (the
    // factory address holds the BPM id).
    paramCursor_ = static_cast<uint8_t>(selectedTarget().address.paramId);
    lastActionRejected_ = false;
    return static_cast<uint8_t>(slot);
}

bool ModulationPatternView::removeSelectedTarget() {
    if (!hasSelectedTarget()) return false;
    const bool released = registry_.releaseTarget(selectedTargetSlot_);
    if (released) selectedTargetSlot_ = kNoTarget;
    return released;
}

bool ModulationPatternView::selectTargetSlot(uint8_t slot) {
    if (slot >= ModulationRegistry::MAX_TARGETS) return false;
    if (!registry_.targetActive(slot)) return false;  // released/never claimed
    selectedTargetSlot_ = slot;
    // The picker cursor mirrors the newly selected target's binding.
    paramCursor_ = static_cast<uint8_t>(selectedTarget().address.paramId);
    return true;
}

void ModulationPatternView::cycleTargetSlot(int delta) {
    // A bounded wrap over the fixed slot pool: the next ACTIVE slot in the
    // delta direction (at most one full sweep of MAX_TARGETS slots).
    if (activeTargetCount() == 0) return;  // nothing to cycle
    int cursor = (selectedTargetSlot_ == kNoTarget)
                     ? 0
                     : static_cast<int>(selectedTargetSlot_);
    for (uint8_t k = 0; k < ModulationRegistry::MAX_TARGETS; ++k) {
        cursor = wrapMod(cursor + delta,
                         static_cast<int>(ModulationRegistry::MAX_TARGETS));
        if (registry_.targetActive(static_cast<size_t>(cursor))) {
            selectedTargetSlot_ = static_cast<uint8_t>(cursor);
            paramCursor_ =
                static_cast<uint8_t>(selectedTarget().address.paramId);
            return;
        }
    }
}

uint8_t ModulationPatternView::selectedTargetSlot() const {
    return selectedTargetSlot_;
}

bool ModulationPatternView::hasSelectedTarget() const {
    return selectedTargetSlot_ != kNoTarget &&
           registry_.targetActive(selectedTargetSlot_);
}

bool ModulationPatternView::lastActionRejected() const {
    return lastActionRejected_;
}

// --- the target's driving pattern ------------------------------------------------

bool ModulationPatternView::setTargetPattern(uint8_t patternIndex) {
    if (!hasSelectedTarget()) return false;
    if (patternIndex >= ModulationRegistry::MAX_PATTERNS) return false;
    selectedTarget().patternIndex = patternIndex;
    return true;
}

void ModulationPatternView::cycleTargetPattern(int delta) {
    if (!hasSelectedTarget()) return;
    sequencer::ModulationTarget& target = selectedTarget();
    target.patternIndex = static_cast<uint8_t>(wrapMod(
        static_cast<int>(target.patternIndex) + delta,
        static_cast<int>(ModulationRegistry::MAX_PATTERNS)));
}

uint8_t ModulationPatternView::targetPattern() const {
    return selectedTarget().patternIndex;
}

// --- the parameter picker (FR12) -------------------------------------------------

uint8_t ModulationPatternView::paramCount() const {
    return sequencer::kParamIdCount;  // the FULL table, BPM included
}

bool ModulationPatternView::paramAt(uint8_t index, ParamOption& out) const {
    // Metadata straight from the ParameterBus (the task-16 table it
    // resolves every address through).
    const sequencer::ParamInfo* info =
        bus_.paramInfo(static_cast<ParamId>(index));
    if (info == nullptr) return false;  // out of the table
    out.name = info->name;
    out.minValue = info->minValue;
    out.maxValue = info->maxValue;
    out.scope = info->scope;
    return true;
}

uint8_t ModulationPatternView::selectedParam() const { return paramCursor_; }

bool ModulationPatternView::selectParam(uint8_t index) {
    if (index >= paramCount()) return false;
    if (!hasSelectedTarget()) return false;
    ParamOption option;
    if (!paramAt(index, option)) return false;  // defensive; total
    sequencer::ModulationTarget& target = selectedTarget();
    // Bind the whole address: the table's scope rides along, so an
    // explicitly selected parameter is always VALIDLY addressed.
    target.address.paramId = static_cast<ParamId>(index);
    target.address.scope = option.scope;
    if (option.scope == ParameterScope::Pattern) {
        target.address.patternIndex = addressPatternSlot_;
    }
    paramCursor_ = index;
    return true;
}

void ModulationPatternView::cycleParam(int delta) {
    paramCursor_ = static_cast<uint8_t>(
        wrapMod(static_cast<int>(paramCursor_) + delta,
                static_cast<int>(paramCount())));
}

// --- scope (FR13) ----------------------------------------------------------------

sequencer::ParameterScope ModulationPatternView::targetScope() const {
    // Total: the FR13 Pattern default reads back with nothing selected.
    return selectedTarget().address.scope;
}

bool ModulationPatternView::setTargetScope(ParameterScope scope) {
    if (!hasSelectedTarget()) return false;
    if (scope != ParameterScope::Pattern && scope != ParameterScope::Global) {
        return false;  // a corrupt scope byte is refused, never stored
    }
    sequencer::ModulationTarget& target = selectedTarget();
    const sequencer::ParamInfo* info =
        bus_.paramInfo(target.address.paramId);
    const bool matches =
        (info != nullptr && info->scope == scope);
    if (matches) {
        target.address.scope = scope;  // the bound parameter fits the scope
    } else {
        // Re-point a scope-mismatched parameter so the address stays VALID:
        // the global scope keeps only the BPM; the per-pattern scope falls
        // back to the first per-pattern parameter (see the class comment).
        target.address.paramId = (scope == ParameterScope::Global)
                                     ? ParamId::GlobalBpm
                                     : static_cast<ParamId>(1);
        target.address.scope = scope;
        paramCursor_ = static_cast<uint8_t>(target.address.paramId);
    }
    if (scope == ParameterScope::Pattern) {
        target.address.patternIndex = addressPatternSlot_;
    }
    return true;
}

void ModulationPatternView::toggleTargetScope() {
    setTargetScope((targetScope() == ParameterScope::Global)
                       ? ParameterScope::Pattern
                       : ParameterScope::Global);
}

bool ModulationPatternView::selectAddressPatternSlot(uint8_t slot) {
    if (slot >= common::PATTERN_CAPACITY) return false;
    addressPatternSlot_ = slot;
    return true;
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
    selectedTarget().scaleFactor = static_cast<uint8_t>(
        clampInt(value, 0, kMaxStepValue));
    return true;
}

void ModulationPatternView::adjustTargetScaling(int delta) {
    setTargetScaling(static_cast<int>(targetScaling()) + delta);
}

int8_t ModulationPatternView::targetBalance() const {
    return selectedTarget().balance;
}

bool ModulationPatternView::setTargetBalance(int value) {
    if (!hasSelectedTarget()) return false;
    selectedTarget().balance =
        static_cast<int8_t>(clampInt(value, kMinBalance, kMaxBalance));
    return true;
}

void ModulationPatternView::adjustTargetBalance(int delta) {
    setTargetBalance(static_cast<int>(targetBalance()) + delta);
}

sequencer::StepDuration ModulationPatternView::targetStepDuration() const {
    return selectedTarget().stepDuration;
}

bool ModulationPatternView::setStepDuration(StepUnit unit, int length) {
    if (!hasSelectedTarget()) return false;
    if (unit != StepUnit::Beat && unit != StepUnit::Bar) {
        return false;  // a foreign unit byte is refused
    }
    sequencer::ModulationTarget& target = selectedTarget();
    target.stepDuration.unit = unit;
    target.stepDuration.length = static_cast<uint16_t>(
        clampInt(length, 1, kMaxDurationLength));
    return true;
}

bool ModulationPatternView::setStepDurationLength(int length) {
    if (!hasSelectedTarget()) return false;
    selectedTarget().stepDuration.length = static_cast<uint16_t>(
        clampInt(length, 1, kMaxDurationLength));
    return true;
}

bool ModulationPatternView::setStepDurationUnit(StepUnit unit) {
    if (!hasSelectedTarget()) return false;
    if (unit != StepUnit::Beat && unit != StepUnit::Bar) {
        return false;  // a foreign unit byte is refused
    }
    selectedTarget().stepDuration.unit = unit;
    return true;
}

void ModulationPatternView::toggleStepDurationUnit() {
    if (!hasSelectedTarget()) return;
    sequencer::ModulationTarget& target = selectedTarget();
    target.stepDuration.unit = (target.stepDuration.unit == StepUnit::Beat)
                                   ? StepUnit::Bar
                                   : StepUnit::Beat;
}

sequencer::ModulationOrder ModulationPatternView::targetOrder() const {
    return selectedTarget().order;
}

bool ModulationPatternView::setTargetOrder(ModulationOrder order) {
    if (!hasSelectedTarget()) return false;
    switch (order) {
        case ModulationOrder::Forward:
        case ModulationOrder::Reverse:
        case ModulationOrder::PingPong:
        case ModulationOrder::Random:
            selectedTarget().order = order;
            return true;
    }
    return false;  // a foreign ordinal is refused, never stored
}

void ModulationPatternView::cycleTargetOrder(int delta) {
    if (!hasSelectedTarget()) return;
    sequencer::ModulationTarget& target = selectedTarget();
    target.order = static_cast<ModulationOrder>(wrapMod(
        static_cast<int>(target.order) + delta, 4));
}

// --- field selection ----------------------------------------------------------------

bool ModulationPatternView::selectField(Field field) {
    const uint8_t ordinal = static_cast<uint8_t>(field);
    if (ordinal >= kFieldCount) return false;  // a foreign enum byte is refused
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

// --- IView ---------------------------------------------------------------------------

void ModulationPatternView::onEnter() {
    std::printf("Entering Modulation Pattern View\n");
    ledMatrix_.clear();
}

state::UIState ModulationPatternView::handleEvent(const state::UIState& state,
                                                   const events::Event& event) {
    // All modulation selection state is view-local (UIState carries no
    // modulation fields), so every path returns the state unchanged. The one
    // UI-state mirror: per-pattern target addresses follow the UI's selected
    // pattern.
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }
    if (state.selectedPattern < common::PATTERN_CAPACITY) {
        addressPatternSlot_ = state.selectedPattern;
    }

    const bool coarse = (event.data.key.mods & mod::SHIFT) != 0;
    const int step = coarse ? kCoarseStep : 1;
    switch (combo(event.data.key.id, event.data.key.mods & ~mod::SHIFT)) {
        case combo(KeyId::LEFT):
            if (field_ == Field::Step) {
                // Move the step cursor within the length; at the edge the
                // field cycles (the pager convention, see the class comment).
                if (!cycleStep(-1)) cycleField(-1);
            } else {
                cycleField(-1);
            }
            break;
        case combo(KeyId::RIGHT):
            if (field_ == Field::Step) {
                if (!cycleStep(+1)) cycleField(+1);
            } else {
                cycleField(+1);
            }
            break;
        case combo(KeyId::UP):
            adjustSelectedField(+step);
            break;
        case combo(KeyId::DOWN):
            adjustSelectedField(-step);
            break;
        case combo(KeyId::ENTER):
            if (field_ == Field::Target) {
                addTarget();  // a full registry latches lastActionRejected_
            } else if (field_ == Field::Duration) {
                toggleStepDurationUnit();
            }
            break;
        default:
            break;
    }
    return state;
}

void ModulationPatternView::render(const state::UIState& state) {
    (void)state;  // the modulation selection state is view-local
    ledMatrix_.clear();
    switch (field_) {
        case Field::Pattern:
            ledMatrix_.drawLabel(kPatternLabel, 0xFFFFEE00);
            ledMatrix_.drawNumber(editedPattern_, 0xFF0011EE);
            break;
        case Field::Length:
            ledMatrix_.drawLabel(kLengthLabel, 0xFFFFEE00);
            ledMatrix_.drawNumber(patternLength(), 0xFF11EE00);
            break;
        case Field::Step: {
            ledMatrix_.drawLabel(kStepLabel, 0xFFFFEE00);
            // One pixel per in-length step (row 0); the cursor is the bright
            // pixel. Steps past the current length stay dark — only
            // in-length steps are highlighted.
            const int length = static_cast<int>(patternLength());
            for (uint8_t i = 0; i < ModulationPattern::MAX_STEPS; ++i) {
                if (static_cast<int>(i) < length) {
                    ledMatrix_.setPixel(i, 0, 0xFF003300);
                }
            }
            ledMatrix_.setPixel(stepCursor_ % ModulationPattern::MAX_STEPS, 0,
                                0xFFFFFF00);
            ledMatrix_.drawNumber(stepValue(stepCursor_), 0xFF11EE00);
            break;
        }
        case Field::Target:
            ledMatrix_.drawLabel(kTargetLabel, 0xFFFFEE00);
            if (hasSelectedTarget()) {
                ledMatrix_.drawNumber(selectedTargetSlot_, 0xFF0011EE);
            } else {
                ledMatrix_.drawLabel(kNoneLabel, 0xFFFF0000);
            }
            break;
        case Field::PatternBind:
            ledMatrix_.drawLabel(kBindLabel, 0xFFFFEE00);
            ledMatrix_.drawNumber(targetPattern(), 0xFF0011EE);
            break;
        case Field::Param:
            ledMatrix_.drawLabel(kParamLabel, 0xFFFFEE00);
            ledMatrix_.drawNumber(selectedParam(), 0xFF11EE00);
            break;
        case Field::Scope:
            ledMatrix_.drawLabel(kScopeLabel, 0xFFFFEE00);
            // 0 = per-pattern (the FR13 default), 1 = global.
            ledMatrix_.drawNumber(
                (targetScope() == ParameterScope::Global) ? 1 : 0, 0xFFEE0011);
            break;
        case Field::Scale:
            ledMatrix_.drawLabel(kScaleLabel, 0xFFFFEE00);
            ledMatrix_.drawNumber(targetScaling(), 0xFF11EE00);
            break;
        case Field::Balance:
            ledMatrix_.drawLabel(kBalanceLabel, 0xFFFFEE00);
            ledMatrix_.drawNumber(targetBalance(), 0xFF11EE00);
            break;
        case Field::Duration: {
            ledMatrix_.drawLabel(kDurationLabel, 0xFFFFEE00);
            const sequencer::StepDuration duration = targetStepDuration();
            ledMatrix_.drawNumber(duration.length, 0xFF11EE00);
            // The unit as a corner pixel: dim = beat, bright = bar.
            ledMatrix_.setPixel(
                0, 15,
                (duration.unit == StepUnit::Bar) ? 0xFFFFFF00 : 0xFF224400);
            break;
        }
        case Field::Order:
            ledMatrix_.drawLabel(kOrderLabel, 0xFFFFEE00);
            ledMatrix_.drawNumber(static_cast<int>(targetOrder()), 0xFFEE0011);
            break;
    }
    if (lastActionRejected_) {
        // The rejected-action indicator: a lit corner pixel (a full target
        // registry), never a blocked retry.
        ledMatrix_.setPixel(15, 15, 0xFFFF0000);
    }
}

// --- private helpers -------------------------------------------------------------------

sequencer::ModulationTarget& ModulationPatternView::selectedTarget() {
    // Total: an unselected / out-of-range slot resolves to the registry's
    // shared null target (factory values), so READS are always safe. Every
    // writer checks hasSelectedTarget() first — an edit never lands in the
    // null target.
    const size_t slot = (selectedTargetSlot_ == kNoTarget)
                            ? ModulationRegistry::INVALID_INDEX
                            : static_cast<size_t>(selectedTargetSlot_);
    return registry_.target(slot);
}

const sequencer::ModulationTarget& ModulationPatternView::selectedTarget() const {
    const size_t slot = (selectedTargetSlot_ == kNoTarget)
                            ? ModulationRegistry::INVALID_INDEX
                            : static_cast<size_t>(selectedTargetSlot_);
    return registry_.target(slot);
}

sequencer::ModulationPattern& ModulationPatternView::editedPatternRef() {
    return registry_.pattern(editedPattern_);
}

const sequencer::ModulationPattern& ModulationPatternView::editedPatternRef() const {
    return registry_.pattern(editedPattern_);
}

void ModulationPatternView::clampStepCursor() {
    const int length = static_cast<int>(patternLength());
    if (length <= 0) {
        stepCursor_ = 0;
        return;
    }
    if (static_cast<int>(stepCursor_) >= length) {
        stepCursor_ = static_cast<uint8_t>(length - 1);
    }
}

int ModulationPatternView::clampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void ModulationPatternView::adjustSelectedField(int step) {
    switch (field_) {
        case Field::Pattern:
            cycleEditedPattern(step);
            break;
        case Field::Length:
            adjustPatternLength(step);
            break;
        case Field::Step:
            adjustStepValue(step);
            break;
        case Field::Target:
            cycleTargetSlot(step >= 0 ? +1 : -1);  // direction only
            break;
        case Field::PatternBind:
            cycleTargetPattern(step);
            break;
        case Field::Param:
            cycleParam(step);
            break;
        case Field::Scope:
            toggleTargetScope();
            break;
        case Field::Scale:
            adjustTargetScaling(step);
            break;
        case Field::Balance:
            adjustTargetBalance(step);
            break;
        case Field::Duration:
            setStepDurationLength(
                static_cast<int>(targetStepDuration().length) + step);
            break;
        case Field::Order:
            cycleTargetOrder(step >= 0 ? +1 : -1);  // direction only
            break;
    }
}

}  // namespace ui
