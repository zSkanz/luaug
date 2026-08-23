#!/usr/bin/env bash
# The two profiles nothing else in this repository builds, compiled and linked.
#
# `shipping` is here because that is how it came to be broken for an unknown
# length of time (D056): `LUAUG_LUAU_COMPILER=OFF` and `LUAUG_DEBUG_UI=OFF` are
# set for it alone, so every `#if` around them is code no other gate ever reads.
#
# **`player` is here for the same reason and it is newer** (D057). It is the
# profile `luaug build` packages -- the Luau compiler ON, the debug overlay OFF,
# a combination no other profile has -- so it is a SHIPPED artifact that,
# without this, no gate would ever compile. That was precisely the objection
# that ruled out "just ship the shipping profile": a profile nothing builds is a
# profile nobody knows is broken.
#
# WHAT THIS BUILDS, AND WHY IT IS NOT THE WHOLE TREE.
#
# `luaug_host` -- the shipping binary itself, compiled and linked, and nothing
# else. A whole second profile at full breadth is not free: the dev build is
# already ~700 s cold, and the parts this leaves out (the C++ test suite, which
# the shipping profile does not build at all because it drives the engine
# through Luau source; `assetc`, whose assimp dependency is the largest single
# compile in the tree; the shader toolchain) are proved by the dev stage on
# every run and cannot rot differently here -- they compile with the same flags
# in both. What CAN rot differently is exactly what the two profile options
# gate, and all of it is reachable from this one executable. Linking rather
# than compiling only, because half of ADR 0011's claim is a link-time one: a
# shipping binary contains no ImGui because no ImGui target was declared, and a
# call site that survived an `#ifdef` sweep shows up as an unresolved symbol
# rather than as an error in a translation unit.
#
# Warm this is a few seconds. Cold -- the first run, or after a `third_party`
# change -- it is a full Release build of the vendored tree, once.
#
# Tier-2 (Linux/Clang) rather than Tier-1 by default: warnings are errors on
# both, Clang diagnoses more than MSVC, and it is the 1x platform if this is
# ever wired into CI. Override with LUAUG_SHIPPING_PRESET to check another one.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

preset="${LUAUG_SHIPPING_PRESET:-linux-clang-shipping}"
playerPreset="${LUAUG_PLAYER_PRESET:-linux-clang-player}"

if [[ -z "${LUAUG_BUILD_ROOT:-}" ]]; then
    echo "shipping-build: LUAUG_BUILD_ROOT is not set (rule R14: builds are out-of-tree)" >&2
    exit 1
fi

case "$LUAUG_BUILD_ROOT" in
"$PWD"*)
    echo "shipping-build: LUAUG_BUILD_ROOT must be OUTSIDE the repository tree (rule R14)" >&2
    exit 1
    ;;
esac

echo "== configure ($preset) =="
cmake --preset "$preset"

echo "== build (luaug_host) =="
cmake --build --preset "$preset" --target luaug_host

echo "== configure ($playerPreset) =="
cmake --preset "$playerPreset"

echo "== build (luaug_host) =="
cmake --build --preset "$playerPreset" --target luaug_host
