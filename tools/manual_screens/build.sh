#!/bin/sh
# Regenerates docs/user/img/*.svg from the real view code. Run from anywhere.
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$here/../..
out=${1:-$root/docs/user/img}; mkdir -p "$out"
S=$root/src
g++ -std=c++17 -O0 -I"$here" -I"$S" -I"$root/tests/stubs" -o "${TMPDIR:-/tmp}/genseq_manual_screens" \
  "$here/gen.cpp" "$here/stub.cpp" "$root/tests/stubs/command_stub.cpp" \
  $S/common/gate_set.cpp $S/common/pitch_set.cpp $S/common/Scale.cpp $S/ui/KeyNames.cpp \
  $S/ui/state/Reducer.cpp $S/ui/state/SetLibrary.cpp $S/ui/views/*.cpp
"${TMPDIR:-/tmp}/genseq_manual_screens" "$out"
