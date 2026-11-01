// generative-engine-expansion task 10.4 — GEN_PARAMS_SET FIFO transport
// round-trip (host).
//
// Validates that commands::sendGenParams encodes a GenerationParameters onto the
// cross-core FIFO and commands::receiveCommandInto decodes it back into the
// PatternSnapshot's GenerationParameters unchanged, and that applying that
// snapshot to the Sequencer sets the pattern's gen-params (so every UI control
// reaches core1 through the REAL path, not just the direct setter).
//
// command.cpp uses multicore_fifo_push/pop_blocking; on host those are not
// provided by the Pico stub (command.cpp is normally not compiled host-side), so
// THIS test supplies a std::queue-backed FIFO double and compiles command.cpp
// against it. Own main(), excluded from the glob.

#include <cstdint>
#include <cstdio>
#include <queue>

// --- Host FIFO double: a simple word queue backing the multicore FIFO API that
// command.cpp calls. The stub header (pico/multicore.h) DECLARES these; we
// DEFINE them here with a std::queue so a full encode->decode round-trip runs. --
static std::queue<uint32_t> g_fifo;

void multicore_fifo_push_blocking(uint32_t v) { g_fifo.push(v); }
uint32_t multicore_fifo_pop_blocking(void) {
    if (g_fifo.empty()) return 0;
    const uint32_t v = g_fifo.front(); g_fifo.pop(); return v;
}
bool multicore_fifo_rvalid(void) { return !g_fifo.empty(); }

#include "command.h"
#include "PatternSnapshot.h"
#include "GenerationParameters.h"

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

}  // namespace

int main() {
    using common::GenerationParameters;

    // Build a non-default GenerationParameters touching every field.
    GenerationParameters in;
    in.scale = common::ScaleId::MINOR_NATURAL;
    in.scaleRoot = 9;
    in.quantize = common::QuantizeMode::SKIP;
    in.patternProbability = 55;
    in.humanizeRange = 12;
    in.seedSet = true;
    in.seed = 0xDEADBEEFu;
    in.coupling = common::CouplingMode::LOCKED;
    in.stepLength = 6;
    for (uint8_t i = 0; i < GenerationParameters::kMaxSteps; ++i) {
        in.stepProbability[i] = (i % 2 == 0) ? static_cast<uint8_t>(i * 5)
                                             : common::STEP_PROB_INHERIT;
    }

    // Encode onto the FIFO, then decode via the allocation-free receive path.
    commands::sendGenParams(/*patternIndex=*/3, in);
    sequencer::PatternSnapshot snap;
    const commands::CommandMessage msg = commands::receiveCommandInto(snap);

    check(msg.cmd == commands::Command::GEN_PARAMS_SET, "decoded command is GEN_PARAMS_SET");
    check(msg.param1 == 3, "pattern index round-trips");

    const GenerationParameters& out = snap.generationParameters();
    check(out.scale == in.scale, "scale round-trips");
    check(out.scaleRoot == in.scaleRoot, "scaleRoot round-trips");
    check(out.quantize == in.quantize, "quantize round-trips");
    check(out.patternProbability == in.patternProbability, "patternProbability round-trips");
    check(out.humanizeRange == in.humanizeRange, "humanizeRange round-trips");
    check(out.seedSet == in.seedSet, "seedSet round-trips");
    check(out.seed == in.seed, "seed (full u32) round-trips");
    check(out.coupling == in.coupling, "coupling round-trips");
    check(out.stepLength == in.stepLength, "stepLength round-trips");
    bool stepsOk = true;
    for (uint8_t i = 0; i < GenerationParameters::kMaxSteps; ++i) {
        if (out.stepProbability[i] != in.stepProbability[i]) stepsOk = false;
    }
    check(stepsOk, "all 16 per-step probabilities round-trip (incl. INHERIT)");

    check(g_fifo.empty(), "FIFO fully drained (payload word count matches encode)");

    if (failures == 0) {
        std::printf("example_genparams_fifo: all checks passed\n");
        return 0;
    }
    std::printf("example_genparams_fifo: %d check(s) failed\n", failures);
    return 1;
}
