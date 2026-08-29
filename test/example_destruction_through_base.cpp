// Feature: midi-output-interface, Example test: destruction through the base interface
//
// Validates: Requirements 1.2.
//
// Requirement 1.2: THE IMidiOutput SHALL declare a virtual destructor such that a derived
// Transport can be safely destroyed through an IMidiOutput base pointer or reference without
// leaking resources.
//
// This is an EXAMPLE test (not property-based). It heap-allocates a derived IMidiOutput whose
// destructor sets a flag, deletes it through an IMidiOutput* base pointer, and asserts the
// derived destructor ran. If IMidiOutput's destructor were non-virtual, deleting through the
// base pointer would be undefined behavior and the derived destructor would not run, leaving
// the flag unset. A virtual destructor guarantees the derived destructor executes.
//
// Host-side only: includes only IMidiOutput.h (SDK-free) plus the C++ standard library. It
// links none of the Pico SDK hardware libraries.

#include "IMidiOutput.h"

#include <cstdint>
#include <cstdio>

namespace {

// Derived test double whose destructor records that it ran by setting *flag_ to true.
class FlaggingMidiOutput : public sequencer::IMidiOutput {
public:
    explicit FlaggingMidiOutput(bool* flag) : flag_(flag) {}

    // Marks the derived destructor as having run. Reached only if the base
    // destructor is virtual when deleting through an IMidiOutput* base pointer.
    ~FlaggingMidiOutput() override { *flag_ = true; }

    void write(uint8_t /*byte*/) override {}

private:
    bool* flag_;
};

}  // namespace

int main() {
    bool derivedDestructorRan = false;

    // Heap-allocate the derived object, but hold it through a base-interface pointer.
    sequencer::IMidiOutput* output = new FlaggingMidiOutput(&derivedDestructorRan);

    // Delete through the base pointer. With a virtual base destructor this runs the
    // derived (FlaggingMidiOutput) destructor, which sets the flag.
    delete output;

    if (!derivedDestructorRan) {
        std::fprintf(stderr,
                     "FAIL: derived destructor did not run when deleting through "
                     "IMidiOutput* (base destructor is not virtual?)\n");
        return 1;
    }

    std::printf("PASS: derived destructor ran when deleting through IMidiOutput*\n");
    return 0;
}
