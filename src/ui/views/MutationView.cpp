// Mutation trigger view (generative-engine-expansion task 20, FR5-FR10,
// FR15). See MutationView.h for the design; this translation unit holds the
// request-assembly logic, the CommandInbox posting and the key-event front
// end.

#include "MutationView.h"

#include "../Types.h"

#include <cstdio>

namespace ui {

namespace {

// The 3-character matrix labels for the four fields (drawLabel takes exactly
// a 4-byte array: three characters plus a trailing byte).
constexpr char kOperatorLabel[4] = {'O', 'p', 'r', '\0'};
constexpr char kTargetLabel[4] = {'T', 'g', 't', '\0'};
constexpr char kIntensityLabel[4] = {'I', 'n', 't', '\0'};
constexpr char kTimingLabel[4] = {'T', 'i', 'm', '\0'};
constexpr char kNoneLabel[4] = {'N', 'o', 'n', '\0'};

// The seven FR7 targets in the fixed documented order the target cursor
// walks (matches MutationConstraints.h's bit order).
constexpr sequencer::MutationTarget kTargetOrder[sequencer::kMutationTargetCount] = {
    sequencer::MutationTarget::GATE_DENSITY,
    sequencer::MutationTarget::PITCH_CONTENTS,
    sequencer::MutationTarget::PITCH_ORDERING,
    sequencer::MutationTarget::VELOCITY_CONTENTS,
    sequencer::MutationTarget::VELOCITY_ORDERING,
    sequencer::MutationTarget::NOTE_DURATION,
    sequencer::MutationTarget::EUCLIDEAN_PARAMS,
};

// Euclidean modulo (the C++ % of a negative value is negative): maps any
// `value` into [0, modulus). Bounded O(1) arithmetic, no loops.
int wrapMod(int value, int modulus) {
    const int wrapped = value % modulus;
    return wrapped < 0 ? wrapped + modulus : wrapped;
}

// Clamp into the FR6 intensity range 0-127.
uint8_t clampIntensity(int value) {
    if (value < 0) return 0;
    if (value > MutationView::kMaxIntensity) {
        return static_cast<uint8_t>(MutationView::kMaxIntensity);
    }
    return static_cast<uint8_t>(value);
}

}  // namespace

MutationView::MutationView(sequencer::MutationOperatorRegistry& registry,
                           sequencer::CommandInbox& inbox,
                           hardware::LedMatrix& ledMatrix)
    : registry_(registry), inbox_(inbox), ledMatrix_(ledMatrix) {
    // A fresh view shows the first registered operator with all of its
    // supported targets selected (an empty registry simply has none).
    refreshOperatorDefaults();
}

// --- operator selection (FR5) -----------------------------------------------------

uint8_t MutationView::operatorCount() const { return registry_.count(); }

const sequencer::IMutationOperator* MutationView::operatorAt(
    uint8_t index) const {
    return registry_.at(index);
}

bool MutationView::selectOperator(uint8_t index) {
    if (index >= operatorCount()) return false;  // refused, never clamped
    operatorIndex_ = index;
    refreshOperatorDefaults();
    return true;
}

void MutationView::cycleOperator(int delta) {
    const int count = operatorCount();
    if (count == 0) return;  // nothing to cycle
    operatorIndex_ = static_cast<uint8_t>(
        wrapMod(static_cast<int>(operatorIndex_) + delta, count));
    refreshOperatorDefaults();
}

const char* MutationView::selectedOperatorId() const {
    const sequencer::IMutationOperator* op = selectedOperator();
    return (op != nullptr) ? op->id() : "";
}

const char* MutationView::selectedOperatorName() const {
    const sequencer::IMutationOperator* op = selectedOperator();
    return (op != nullptr) ? op->name() : "";
}

uint8_t MutationView::selectedOperatorIndex() const { return operatorIndex_; }

// --- target selection (FR7, restricted by FR8 operator control) -------------------

sequencer::MutationTargetMask MutationView::supportedTargets() const {
    const sequencer::IMutationOperator* op = selectedOperator();
    return (op != nullptr) ? op->supportedTargets() : 0;
}

sequencer::MutationTargetMask MutationView::targetMask() const {
    return targetMask_;
}

bool MutationView::hasTarget(sequencer::MutationTarget target) const {
    return (targetMask_ & sequencer::maskOf(target)) != 0;
}

bool MutationView::toggleTarget(sequencer::MutationTarget target) {
    const sequencer::MutationTargetMask bit = sequencer::maskOf(target);
    // FR8 operator control: the mask can only ever include the selected
    // operator's supportedTargets().
    if ((bit & supportedTargets()) == 0) return false;
    targetMask_ = static_cast<sequencer::MutationTargetMask>(targetMask_ ^ bit);
    return true;
}

void MutationView::selectAllSupportedTargets() {
    targetMask_ = supportedTargets();
}

void MutationView::clearTargets() { targetMask_ = 0; }

// --- the FR7 target cursor ---------------------------------------------------------

sequencer::MutationTarget MutationView::targetAt(uint8_t index) const {
    if (index >= sequencer::kMutationTargetCount) {
        return sequencer::MutationTarget::NONE;  // total accessor
    }
    return kTargetOrder[index];
}

bool MutationView::selectTargetIndex(uint8_t index) {
    if (index >= sequencer::kMutationTargetCount) return false;
    targetCursor_ = index;
    return true;
}

uint8_t MutationView::selectedTargetIndex() const { return targetCursor_; }

// --- intensity (FR6) ------------------------------------------------------------------

uint8_t MutationView::intensity() const { return intensity_; }

void MutationView::setIntensity(int value) {
    intensity_ = clampIntensity(value);
}

void MutationView::adjustIntensity(int delta) {
    setIntensity(static_cast<int>(intensity_) + delta);
}

// --- the FR10 level-1 per-invocation timing override ----------------------------

bool MutationView::timingOverrideActive() const { return timingActive_; }

sequencer::OperationTimingMode MutationView::timingOverride() const {
    return timingMode_;
}

bool MutationView::setTimingOverride(sequencer::OperationTimingMode mode) {
    // Inherit delegates to the levels below — it is not an override.
    if (mode == sequencer::OperationTimingMode::Inherit) return false;
    timingActive_ = true;
    timingMode_ = mode;
    return true;
}

void MutationView::clearTimingOverride() {
    timingActive_ = false;
    timingMode_ = sequencer::OperationTimingMode::Immediate;
}

void MutationView::cycleTimingOverride(int delta) {
    // The cycle's ordinal: 0 absent, 1 Immediate, 2 QuantizeBeat,
    // 3 QuantizeBar (the sign of `delta` picks the direction).
    int ordinal = timingActive_ ? 1 + static_cast<int>(timingMode_) : 0;
    ordinal = wrapMod(ordinal + (delta >= 0 ? 1 : -1), 4);
    if (ordinal == 0) {
        clearTimingOverride();
    } else {
        timingActive_ = true;
        timingMode_ = static_cast<sequencer::OperationTimingMode>(ordinal - 1);
    }
}

// --- actions: posts to the CommandInbox (FR5/FR9/FR15) ---------------------------

sequencer::SequenceId MutationView::trigger(uint8_t patternIndex) {
    const sequencer::IMutationOperator* op = selectedOperator();
    if (op == nullptr) {
        // Nothing is registered: a rejected action, nothing posted.
        lastActionRejected_ = true;
        return sequencer::kInvalidSequence;
    }
    return post(sequencer::makeMutateCommand(op->id(), patternIndex, targetMask_,
                                             intensity_, /*invariants=*/0,
                                             sequencer::kUnlimitedMutationDistance,
                                             timingOverrideForPost()));
}

sequencer::SequenceId MutationView::triggerAllPatterns() {
    return trigger(sequencer::kAllPatterns);
}

sequencer::SequenceId MutationView::requestUndo() {
    return post(sequencer::makeUndoCommand(timingOverrideForPost()));
}

sequencer::SequenceId MutationView::requestRevert() {
    return post(sequencer::makeRevertCommand(timingOverrideForPost()));
}

sequencer::SequenceId MutationView::requestKeep() {
    return post(sequencer::makeKeepCommand(timingOverrideForPost()));
}

bool MutationView::lastActionRejected() const { return lastActionRejected_; }

// --- field selection ------------------------------------------------------------------

bool MutationView::selectField(Field field) {
    const uint8_t ordinal = static_cast<uint8_t>(field);
    if (ordinal >= kFieldCount) return false;  // a foreign enum byte is refused
    field_ = field;
    return true;
}

MutationView::Field MutationView::selectedField() const { return field_; }

void MutationView::cycleField(int delta) {
    field_ = static_cast<Field>(
        wrapMod(static_cast<int>(field_) + delta, kFieldCount));
}

// --- IView -----------------------------------------------------------------------------

void MutationView::onEnter() {
    std::printf("Entering Mutation View\n");
    ledMatrix_.clear();
}

state::UIState MutationView::handleEvent(const state::UIState& state,
                                         const events::Event& event) {
    // The request is assembled view-locally and handed over as a command;
    // UIState carries no mutation fields, so every path here returns the
    // state unchanged.
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    const bool coarse = (event.data.key.mods & mod::SHIFT) != 0;
    const int step = coarse ? kCoarseStep : 1;
    switch (combo(event.data.key.id, event.data.key.mods & ~mod::SHIFT)) {
        case combo(KeyId::LEFT):
            cycleField(-1);
            break;
        case combo(KeyId::RIGHT):
            cycleField(+1);
            break;
        case combo(KeyId::UP):
            adjustSelectedField(+step);
            break;
        case combo(KeyId::DOWN):
            adjustSelectedField(-step);
            break;
        case combo(KeyId::ENTER):
            if (field_ == Field::Target) {
                // Toggle the cursor's target (refused for unsupported
                // targets — FR8 operator control).
                toggleTarget(targetAt(targetCursor_));
            } else {
                trigger(state.selectedPattern);
            }
            break;
        case combo(KeyId::U):
            requestUndo();
            break;
        case combo(KeyId::R):
            requestRevert();
            break;
        case combo(KeyId::K):
            requestKeep();
            break;
        default:
            break;
    }
    return state;
}

void MutationView::render(const state::UIState& state) {
    (void)state;  // the request state is view-local (see the class comment)
    ledMatrix_.clear();
    if (operatorCount() == 0) {
        ledMatrix_.drawLabel(kNoneLabel, 0xFFFFEE00);
        return;
    }
    switch (field_) {
        case Field::Operator:
            ledMatrix_.drawLabel(kOperatorLabel, 0xFFFFEE00);
            ledMatrix_.drawNumber(selectedOperatorIndex(), 0xFFFF0011);
            break;
        case Field::Target:
            ledMatrix_.drawLabel(kTargetLabel, 0xFFFFEE00);
            // The 16x16 matrix shows the FR7 target cursor; supported
            // targets of the selected operator render as toggled bits.
            ledMatrix_.drawNumber(selectedTargetIndex(), 0xFF0011EE);
            break;
        case Field::Intensity:
            ledMatrix_.drawLabel(kIntensityLabel, 0xFFFFEE00);
            ledMatrix_.drawNumber(intensity(), 0xFF11EE00);
            break;
        case Field::Timing:
            ledMatrix_.drawLabel(kTimingLabel, 0xFFFFEE00);
            // 0 = no override (levels 2/3 decide), 1..3 the override mode.
            ledMatrix_.drawNumber(
                timingOverrideActive() ? static_cast<int>(timingOverride()) + 1 : 0,
                0xFFEE0011);
            break;
    }
    if (lastActionRejected_) {
        // The rejected-action indicator: a lit corner pixel (a full inbox /
        // an impossible request), never a blocked retry.
        ledMatrix_.setPixel(15, 15, 0xFFFF0000);
    }
}

// --- private helpers -----------------------------------------------------------------

const sequencer::IMutationOperator* MutationView::selectedOperator() const {
    return registry_.at(operatorIndex_);  // nullptr past the count (total)
}

void MutationView::refreshOperatorDefaults() {
    // A newly selected operator starts with ALL of its supported targets
    // selected, so a fresh trigger is never an empty request; switching
    // operators re-masks (a bit the new operator does not support is
    // dropped — FR8 operator control).
    targetMask_ = supportedTargets();
}

sequencer::GenerativeTimingOverride MutationView::timingOverrideForPost() const {
    sequencer::GenerativeTimingOverride timing{};
    if (timingActive_) {
        timing.active = true;
        timing.mode = static_cast<uint8_t>(timingMode_);
    }
    return timing;
}

sequencer::SequenceId MutationView::post(sequencer::InboxCommand cmd) {
    // tryPush never blocks: a full ring returns kInvalidSequence at once.
    const sequencer::SequenceId seq = inbox_.tryPush(cmd);
    lastActionRejected_ = (seq == sequencer::kInvalidSequence);
    return seq;
}

void MutationView::adjustSelectedField(int step) {
    switch (field_) {
        case Field::Operator:
            cycleOperator(step);
            break;
        case Field::Target:
            targetCursor_ = static_cast<uint8_t>(wrapMod(
                static_cast<int>(targetCursor_) + step,
                sequencer::kMutationTargetCount));
            break;
        case Field::Intensity:
            adjustIntensity(step);
            break;
        case Field::Timing:
            cycleTimingOverride(step >= 0 ? +1 : -1);  // direction only
            break;
    }
}

}  // namespace ui
