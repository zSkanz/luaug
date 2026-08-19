#!/usr/bin/env bash
# Tier-2 build and test. Runs inside the container scripts/docker/tier2.Dockerfile
# builds, and is the same sequence .github/workflows/ci.yml runs on ubuntu.
#
# Kept as a script rather than a docker `CMD` so the two callers -- the local
# gate and the workflow -- run identical commands. When they diverge, "it passes
# locally" stops meaning anything.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

preset="${LUAUG_LINUX_PRESET:-linux-clang-dev}"

if [[ -z "${LUAUG_BUILD_ROOT:-}" ]]; then
    echo "linux-build: LUAUG_BUILD_ROOT is not set (rule R14: builds are out-of-tree)" >&2
    exit 1
fi

case "$LUAUG_BUILD_ROOT" in
"$PWD"*)
    echo "linux-build: LUAUG_BUILD_ROOT must be OUTSIDE the repository tree (rule R14)" >&2
    exit 1
    ;;
esac

echo "== configure ($preset) =="
cmake --preset "$preset"

echo "== build =="
cmake --build --preset "$preset"

echo "== test =="
# The screenshot gate skips here rather than failing: a container has no GPU,
# and `luaug-host` reports that with its own exit code so CTest can tell the
# difference between "no driver" and "the picture changed". The capture gate
# does run, because it needs no GPU at all -- which is exactly why it, and not
# the image comparison, is the blocking render gate (architecture.md §9).
ctest --preset "$preset" --output-on-failure
