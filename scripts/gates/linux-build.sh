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

# rokit's shims are installed in the image but a non-interactive shell does not
# pick them up, the same reason luau-check.sh adds this path.
if [[ -d "$HOME/.rokit/bin" ]]; then
    PATH="$HOME/.rokit/bin:$PATH"
    export PATH
fi

echo "== test =="
# The screenshot gate skips here rather than failing: a container has no GPU,
# and `luaug-host` reports that with its own exit code so CTest can tell the
# difference between "no driver" and "the picture changed". The capture gate
# does run, because it needs no GPU at all -- which is exactly why it, and not
# the image comparison, is the blocking render gate (architecture.md §9).
ctest --preset "$preset" --output-on-failure

# The CLI's own path to the same suite, which the M3 gate requires green "on
# both tiers". It runs the engine the build above produced -- LUAUG_BUILD_ROOT
# is how `luaug` finds it -- and turns the per-case report into TAP. Running it
# after ctest rather than instead of it is deliberate: ctest proves the engine,
# this proves the tool a developer actually types.
echo "== luaug test =="
bash scripts/luaug.sh test tests/conformance > /tmp/luaug-test.tap
tail -n 3 /tmp/luaug-test.tap

# The M3 gate's first item. It starts a dev server, launches this build headless
# against it, mutates a file and waits for the reload to be confirmed over the
# WebSocket -- so it exercises inotify, the socket, the safe point and the world
# hash together, and it is the only thing that does.
echo "== hot reload (end to end) =="
lute test tests/hotreload
