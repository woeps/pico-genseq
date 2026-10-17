#pragma once
// Minimal host stub for Pico SDK multicore.
//
// command.h includes this; only the CommandMessage/Command types are needed
// host-side, and command.cpp (which uses the multicore FIFO) is NOT compiled.
//
// sequencer_task.cpp calls multicore_launch_core1(sequencer_task) where
// sequencer_task is `static void sequencer_task()`. We declare the function here
// (signature void(*)(void), compatible with that call) so sequencer_task.cpp
// compiles host-side. The DEFINITION is intentionally NOT provided here: the
// launch-guard example test (test/example_launch_guard.cpp) supplies a counting
// definition so it can assert the launch happens exactly once (Req 4.4). Targets
// that do not compile sequencer_task.cpp never reference this symbol.
void multicore_launch_core1(void (*entry)(void));

// sequencer_task.cpp calls multicore_lockout_victim_init() at the start of the core1 entry
// (Req 4.4/4.5) so core0 can park core1 during a flash erase/program. Host-side that entry is
// never actually executed (the launch-guard test's multicore_launch_core1 double does not invoke
// the entry function), so an inline no-op definition is sufficient: it lets sequencer_task.cpp
// compile and links without requiring any test source to supply a definition. On real firmware
// the Pico SDK provides the true implementation.
static inline void multicore_lockout_victim_init(void) {
    // No-op host double.
}
