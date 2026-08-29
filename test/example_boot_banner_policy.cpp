// Feature: persist-settings, Example: boot-banner policy (only CRC_MISMATCH warns)
//
// The boot-time banner policy was narrowed so that ONLY LoadStatus::CRC_MISMATCH
// raises the user-facing "saved config not loaded" banner: a well-formed GenSeq
// record of this firmware's format existed (magic + version matched) but its
// payload failed CRC verification. Every other status (OK / ABSENT / BAD_MAGIC /
// BAD_VERSION / TRUNCATED) means no usable save of ours is present and must fall
// back to defaults SILENTLY (no banner). TRUNCATED in particular is the status
// that previously (incorrectly) raised the banner.
//
// This extracts that inline UIController policy into the pure, SDK-free helper
// persistence::shouldWarnOnBootLoad and asserts the full LoadStatus enum maps to
// the correct warn/no-warn decision.
//
// Host-side only: includes the SDK-free persistence headers plus the C++ standard
// library, links no Pico SDK hardware libraries, and carries its own main(), so
// test/CMakeLists.txt builds it as its OWN executable and excludes it from the
// property-test glob (two main()s would fail to link).

#include "persistence/BootBannerPolicy.h"
#include "persistence/SaveRecord.h"

#include <cstdio>

namespace {

// Reports one failed check and flips the shared pass flag.
void expect(bool cond, const char* what, bool& ok) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

}  // namespace

int main() {
    using namespace persistence;

    bool ok = true;

    // Non-warning statuses: no usable save of ours present -> silent defaults.
    expect(shouldWarnOnBootLoad(LoadStatus::OK) == false,
           "OK must NOT warn", ok);
    expect(shouldWarnOnBootLoad(LoadStatus::ABSENT) == false,
           "ABSENT must NOT warn", ok);
    expect(shouldWarnOnBootLoad(LoadStatus::BAD_MAGIC) == false,
           "BAD_MAGIC must NOT warn", ok);
    expect(shouldWarnOnBootLoad(LoadStatus::BAD_VERSION) == false,
           "BAD_VERSION must NOT warn", ok);
    expect(shouldWarnOnBootLoad(LoadStatus::TRUNCATED) == false,
           "TRUNCATED must NOT warn (the status that caused the reported bug)", ok);

    // The one warning status: a real GenSeq save whose payload failed CRC.
    expect(shouldWarnOnBootLoad(LoadStatus::CRC_MISMATCH) == true,
           "CRC_MISMATCH must warn", ok);

    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: boot-banner policy did not match 'only CRC_MISMATCH warns'\n");
        return 1;
    }

    std::printf("PASS: boot-banner policy warns only on CRC_MISMATCH; all other "
                "LoadStatus values fall back to defaults silently\n");
    return 0;
}
