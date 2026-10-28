// Mutation trigger view (generative-engine-expansion task 20, FR5-FR10,
// FR15; refactored onto the command queue by ui-integration T6). See
// MutationView.h for the design; this translation unit holds the fixed
// metadata copy of the built-in operators, the request-assembly logic,
// the command-queue posting and the key-event front end.

#include "MutationView.h"

#include "../../commands/command.h"
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

// Core0's OWN fixed metadata copy of the built-in mutation operators
// (ui-integration T6): each operator's stable id, display name and
// supported-target mask, in the SAME registration order the sequencer
// core's registerBuiltInMutations() uses. The sequencer keeps its own
// registry and resolves the operator INDEX a MUTATE command carries
// through it — no operator object, no registry and no other data
// structure crosses the cores; both sides agree only on this order
// (test_mutation_view pins the mirror against the real registry).
struct OperatorInfo {
    const char* id;
    const char* name;
    sequencer::MutationTargetMask targets;
};

// The shared target mask of the three transform operators (mirrors
// TransformMutations.h's kTransformTargets: they reorder the pitch
// ordering, the velocity ordering and the gate ordering).
constexpr sequencer::MutationTargetMask kTransformTargets =
    sequencer::maskOf(sequencer::MutationTarget::PITCH_ORDERING) |
    sequencer::maskOf(sequencer::MutationTarget::VELOCITY_ORDERING) |
    sequencer::maskOf(sequencer::MutationTarget::EUCLIDEAN_PARAMS);

constexpr OperatorInfo kBuiltInOperators[] = {
    {"builtin.mutate-density", "Density",
     sequencer::maskOf(sequencer::MutationTarget::GATE_DENSITY)},
    {"builtin.mutate-pitch-set", "Pitch Set",
     sequencer::maskOf(sequencer::MutationTarget::PITCH_CONTENTS)},
    {"builtin.mutate-velocity-set", "Velocity Set",
     sequencer::maskOf(sequencer::MutationTarget::VELOCITY_CONTENTS)},
    {"builtin.mutate-duration", "Duration",
     sequencer::maskOf(sequencer::MutationTarget::NOTE_DURATION)},
    {"builtin.mutate-euclidean", "Euclidean Params",
     sequencer::maskOf(sequencer::MutationTarget::EUCLIDEAN_PARAMS)},
    {"builtin.rotate", "Rotate", kTransformTargets},
    {"builtin.invert", "Invert", kTransformTargets},
    {"builtin.shuffle", "Shuffle", kTransformTargets},
    {"builtin.mutate-all", "Mutate All", sequencer::kAllMutationTargets},
    {"builtin.evolve", "Evolve", sequencer::kAllMutationTargets},
};

constexpr uint8_t kBuiltInOperatorCount =
    sizeof(kBuiltInOperators) / sizeof(kBuiltInOperators[0]);

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

MutationView::MutationView(hardware::LedMatrix& ledMatrix) : ledMatrix_(ledMatrix) {
    // A fresh view shows the first built-in operator with all of its
    // supported targets selected.
    refreshOperatorDefaults();
}

// --- operator selection (FR5) -----------------------------------------------------

uint8_t MutationView::operatorCount() { return kBuiltInOperatorCount; }

const char* MutationView::operatorIdAt(uint8_t index) {
    return (index < kBuiltInOperatorCount) ? kBuiltInOperators[index].id : "";
}

const char* MutationView::operatorNameAt(uint8_t index) {
    return (index < kBuiltInOperatorCount) ? kBuiltInOperators[index].name : "";
}

sequencer::MutationTargetMask MutationView::operatorTargetsAt(uint8_t index) {
    return (index < kBuiltInOperatorCount) ? kBuiltInOperators[index].targets : 0;
}

bool MutationView::selectOperator(uint8_t index) {
    if (index >= operatorCount()) return false;  // refused, never clamped
    operatorIndex_ = index;
    refreshOperatorDefaults();
    return true;
}

void MutationView::cycleOperator(int delta) {
    // The fixed metadata table is never empty (it is a compile-time
    // constant), so the wrap always lands on a real operator.
    operatorIndex_ = static_cast<uint8_t>(
        wrapMod(static_cast<int>(operatorIndex_) + delta, operatorCount()));
    refreshOperatorDefaults();
}

const char* MutationView::selectedOperatorId() const {
    return operatorIdAt(operatorIndex_);
}

const char* MutationView::selectedOperatorName() const {
    return operatorNameAt(operatorIndex_);
}

uint8_t MutationView::selectedOperatorIndex() const { return operatorIndex_; }

// --- target selection (FR7, restricted by FR8 operator control) -------------------

sequencer::MutationTargetMask MutationView::supportedTargets() const {
    return operatorTargetsAt(operatorIndex_);
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

sequencer::MutationTarget MutationView::targetAt(uint8_t index) {
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

// --- actions: posts over the command queue (FR5/FR9/FR15) -----------------------

void MutationView::trigger(uint8_t patternIndex) {
    // The MUTATE command (ui-integration T6): the ONLY cross-core path for
    // a mutation request. The operator goes by its INDEX into the shared
    // registration order; the FR7 mask, the FR6 intensity and the packed
    // FR10 override ride the command's payload words.
    commands::sendMutateCommand(operatorIndex_, patternIndex, targetMask_, intensity_,
                               packedTimingForPost());
}

void MutationView::triggerAllPatterns() {
    trigger(commands::ALL_PATTERNS);
}

void MutationView::requestUndo() {
    commands::sendUndoCommand(packedTimingForPost());
}

void MutationView::requestRevert() {
    commands::sendRevertCommand(packedTimingForPost());
}

void MutationView::requestKeep() {
    commands::sendKeepCommand(packedTimingForPost());
}

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
    // ESCAPE (pressed once) returns to the PATTERNS parent view; held
    // repeats and releases are ignored, matching the sibling views.
    if (event.type == events::EventType::KEY_PRESSED &&
        event.data.key.id == KeyId::ESCAPE) {
        state::UIState next = state;
        next.currentView = state::ViewId::PATTERNS;
        return next;
    }

    // The request is assembled view-locally and handed over as a command;
    // UIState carries no mutation fields, so every other path here returns
    // the state unchanged.
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
}

// --- private helpers -----------------------------------------------------------------

void MutationView::refreshOperatorDefaults() {
    // A newly selected operator starts with ALL of its supported targets
    // selected, so a fresh trigger is never an empty request; switching
    // operators re-masks (a bit the new operator does not support is
    // dropped — FR8 operator control).
    targetMask_ = supportedTargets();
}

uint8_t MutationView::packedTimingForPost() const {
    // The wire form of the FR10 override (command.h): bit 0 active,
    // bits 1-2 the OperationTimingMode ordinal. An ABSENT override packs
    // to 0, so the sequencer's three-level policy applies its own levels.
    if (!timingActive_) return 0;
    return commands::packGenerativeTiming(true,
                                           static_cast<uint8_t>(timingMode_));
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
