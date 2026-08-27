#!/usr/bin/env bash
# The real-image golden suite, on Mesa's software rasterizer.
#
# **Both `docs/architecture.md` §9 and `docs/roadmap.md` promise this** and
# neither had it: "a small real-image golden suite (lavapipe on Linux,
# WARP/D3D12 on Windows) runs nightly, non-blocking". What existed was one
# recorded PNG -- `tests/screenshots/meshes-lavapipe.png` -- with its own README
# saying nothing compares it, because a golden that tries to span a discrete GPU
# and a software rasterizer is a golden that can no longer see a real change.
#
# The thing that makes a comparison legitimate here is that it does NOT span
# them. lavapipe is bit-identical to itself: the same scene rendered twice in
# this image differs in zero pixels at tolerance zero, which is what a software
# rasterizer with no scheduling nondeterminism should do and what was measured
# before this file was written. So these goldens are compared EXACTLY, and a
# single changed pixel is a real change.
#
#   scripts/gates/lavapipe-goldens.sh            # compare
#   scripts/gates/lavapipe-goldens.sh --record   # rewrite the goldens
#
# **Non-blocking**, which is what both documents say and what the option
# `LUAUG_LAVAPIPE_GOLDENS` is for: nothing registers these tests unless it is
# asked to. A Mesa upgrade in the base image WILL move every pixel of every one
# of them, and that must not be able to redden a gate somebody runs before a
# push. It runs nightly, and locally through `localgate.ps1 -Only lavapipe`.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

preset="${LUAUG_LAVAPIPE_PRESET:-linux-clang-dev}"
record=0
[[ "${1:-}" == "--record" ]] && record=1

if [[ -z "${LUAUG_BUILD_ROOT:-}" ]]; then
    echo "lavapipe-goldens: LUAUG_BUILD_ROOT is not set (rule R14: builds are out-of-tree)" >&2
    exit 1
fi

echo "== the device this tier has =="
# Printed rather than assumed. A run that silently fell back to a different ICD
# would produce a wall of differing pixels and no explanation, and the driver
# string is the first thing anybody would want.
vulkaninfo --summary 2>/dev/null | grep -E 'driverName|driverInfo|deviceName' | head -3 || \
    echo "  (vulkaninfo unavailable; the host reports its own device below)"

cmake --preset "$preset" -DLUAUG_LAVAPIPE_GOLDENS=ON >/dev/null
cmake --build --preset "$preset" >/dev/null

build="$LUAUG_BUILD_ROOT/$preset"
host="$build/engine/app/luaug-host"

if [[ $record -eq 1 ]]; then
    # Written straight into the tree, which is mounted. Recording is a
    # deliberate act with a flag on it, never something a comparison run can do
    # -- a gate that rewrites its own expectation is not a gate.
    "$host" examples/02-meshes --headless --frames=30 --exit \
        --screenshot=tests/screenshots/lavapipe/meshes.png
    "$host" tests/screenshots/specular --headless --frames=120 --exit \
        --screenshot=tests/screenshots/lavapipe/specular.png
    "$host" tests/rendercapture/uipanel --headless --frames=2 --exit \
        --screenshot=tests/screenshots/lavapipe/ui.png
    echo "== recorded =="
    ls -l tests/screenshots/lavapipe/
    exit 0
fi

ctest --preset "$preset" --output-on-failure --tests-regex '^lavapipe_golden'
