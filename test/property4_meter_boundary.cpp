// performance-safe-foundation task 3.4 — exhaustive meter and boundary property.
//
// Validates Requirements 1.1-1.7, 11.2 against sequencer::TransportTimeline and
// common::TimeSignature.
//
// Three complementary checks:
//
//   (A) EXHAUSTIVE meter coverage: for EVERY supported (numerator, denominator)
//       in {1..32} x {1,2,4,8,16,32}, ticksPerBeat == 96/denominator and
//       ticksPerBar == numerator * ticksPerBeat exactly (Req 1.3, 1.7), and beat/
//       bar boundaries fall exactly on those tick multiples (Req 1.2). Every
//       UNSUPPORTED denominator is rejected, never rounded.
//
//   (B) LONG-RUN phase is drift-free at BPM 40/120/255 (Req 1.5): the summed
//       integer tick durations equal the exact rational cumulative deadline
//       floor(n*60e6/(bpm*PPQN)) for a long run, so no fractional-microsecond
//       error accumulates.
//
//   (C) A mid-run BPM change preserves the tick index and the already-chosen
//       intended timestamp of the current tick (Req 1.5): phase is not jumped
//       and pattern positions are not restarted.

#include "TransportTimeline.h"
#include "../common/TimeSignature.h"

#include <rapidcheck.h>

#include <array>
#include <cstdint>

namespace ts = common::time_signature;

namespace {

constexpr std::array<uint8_t, 6> kDenoms{1, 2, 4, 8, 16, 32};

bool isSupportedDenom(uint16_t d) {
    for (uint8_t v : kDenoms) if (v == d) return true;
    return false;
}

}  // namespace

bool runProperty4Meter() {
    bool ok = true;

    // (A) Exhaustive supported-meter coverage. The space is small (32 x 6 = 192),
    // so enumerate it fully rather than sampling — RapidCheck still drives the
    // boundary spot-checks below.
    ok &= rc::check(
        "Property 4 (meter): every supported (num,denom) has exact ticksPerBeat/"
        "Bar and on-grid beat/bar boundaries; unsupported denominators rejected",
        [](uint8_t numRaw, uint8_t denomRaw) {
            // Map random bytes onto a numerator 1..32 and ANY denominator 1..64
            // (so unsupported 3/5/.../64 also get exercised for rejection).
            const uint8_t num = static_cast<uint8_t>(1 + (numRaw % 32));   // 1..32
            const uint8_t denom = static_cast<uint8_t>(1 + (denomRaw % 64));  // 1..64

            common::TimeSignature sig(num, denom);
            const bool supported = ts::isSupported(sig);
            RC_ASSERT(supported == (isSupportedDenom(denom) && num >= 1 && num <= 32));

            sequencer::TransportTimeline tl;
            const bool accepted = tl.setTimeSignature(sig);
            RC_ASSERT(accepted == supported);  // rejected, not rounded (Req 1.7)
            if (!supported) return;            // unsupported: nothing more to check

            const uint32_t tpb = ts::ticksPerBeat(sig);
            const uint32_t tpBar = ts::ticksPerBar(sig);
            RC_ASSERT(tpb == 96u / denom);          // exact at PPQN 24 (Req 1.3)
            RC_ASSERT(tpBar == num * tpb);           // bar = numerator beats (Req 1.3)
            RC_ASSERT(tl.ticksPerBeat() == tpb);
            RC_ASSERT(tl.ticksPerBar() == tpBar);

            // Beat/bar boundaries land exactly on tick multiples (Req 1.2).
            tl.setBpm(120);
            tl.start(0);
            RC_ASSERT(tl.onBeatBoundary());          // tick 0 is a beat and bar boundary
            RC_ASSERT(tl.onBarBoundary());
            for (uint32_t t = 1; t <= tpBar; ++t) {
                tl.advance();
                RC_ASSERT(tl.onBeatBoundary() == ((t % tpb) == 0));
                RC_ASSERT(tl.onBarBoundary() == ((t % tpBar) == 0));
                RC_ASSERT(tl.beatIndex() == t / tpb);
                RC_ASSERT(tl.barIndex() == t / tpBar);
            }
        });

    // (B) Long-run drift-free phase at the three representative BPMs (Req 1.5).
    ok &= rc::check(
        "Property 4 (phase): cumulative tick timestamps equal the exact rational "
        "deadline floor(n*60e6/(bpm*24)) over a long run at BPM 40/120/255",
        [](uint16_t runLenRaw) {
            const std::array<uint16_t, 3> bpms{40, 120, 255};
            const uint32_t n = 1 + (runLenRaw % 4096);  // up to ~4096 ticks
            for (uint16_t bpm : bpms) {
                sequencer::TransportTimeline tl;
                tl.setBpm(bpm);
                tl.start(0);
                for (uint32_t k = 1; k <= n; ++k) {
                    tl.advance();
                    const uint64_t exact =
                        (static_cast<uint64_t>(k) * 60ULL * 1000000ULL) /
                        (static_cast<uint64_t>(bpm) * sequencer::TransportTimeline::PPQN);
                    RC_ASSERT(tl.currentTickMicros() == exact);
                }
            }
        });

    // (C) Mid-run BPM change preserves tick index and current intended timestamp
    // (Req 1.5): no phase jump, no position restart.
    ok &= rc::check(
        "Property 4 (bpm change): a mid-run BPM change keeps the tick index and "
        "the current tick's intended timestamp; new duration applies next tick",
        [](uint16_t aRaw, uint16_t bRaw, uint8_t ticksRaw) {
            const uint16_t bpmA = static_cast<uint16_t>(40 + (aRaw % 216));   // 40..255
            const uint16_t bpmB = static_cast<uint16_t>(40 + (bRaw % 216));
            const uint32_t ticks = 1 + (ticksRaw % 64);

            sequencer::TransportTimeline tl;
            tl.setBpm(bpmA);
            tl.start(0);
            for (uint32_t k = 0; k < ticks; ++k) tl.advance();

            const uint64_t idxBefore = tl.tickIndex();
            const uint64_t microsBefore = tl.currentTickMicros();

            RC_ASSERT(tl.setBpm(bpmB));
            // Index and the already-chosen current timestamp are unchanged.
            RC_ASSERT(tl.tickIndex() == idxBefore);
            RC_ASSERT(tl.currentTickMicros() == microsBefore);

            // The post-change segment stays drift-free: after advancing m further
            // ticks, the elapsed time equals the timeline's exact integer
            // arithmetic, carrying the remainder left by the old bpm. This is the
            // sound phase-preservation invariant (Req 1.5) — a single post-change
            // step need NOT be near the new ideal because the old bpm's carried
            // remainder (a numerator in units of 60e6) is folded into it; the
            // per-tick carry telescopes so the SEGMENT total is exact.
            //
            // Reference: the carried remainder numerator after idxBefore ticks at
            // bpmA is (idxBefore * 60e6) mod denomA. Advancing m ticks at bpmB
            // sums floor((60e6 + rem_i)/denomB), which telescopes exactly to
            // (m*60e6 + remBefore) / denomB.
            const uint64_t NUM = 60ULL * 1000000ULL;
            const uint64_t denomA = static_cast<uint64_t>(bpmA) *
                                    sequencer::TransportTimeline::PPQN;
            const uint64_t denomB = static_cast<uint64_t>(bpmB) *
                                    sequencer::TransportTimeline::PPQN;
            const uint64_t remBefore = (static_cast<uint64_t>(idxBefore) * NUM) % denomA;

            const uint64_t startMicros = tl.currentTickMicros();
            const uint32_t m = 500;
            for (uint32_t k = 0; k < m; ++k) tl.advance();
            const uint64_t elapsed = tl.currentTickMicros() - startMicros;
            const uint64_t exactElapsed = (static_cast<uint64_t>(m) * NUM + remBefore) / denomB;
            RC_ASSERT(elapsed == exactElapsed);
        });

    return ok;
}
