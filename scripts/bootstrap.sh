#!/usr/bin/env bash
# LuauG bootstrap (Linux/macOS). Idempotent; safe to re-run.
#
# Exits non-zero if anything required is missing: a bootstrap that reports
# success while a step failed just moves the failure somewhere more confusing.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
problems=()

echo "== LuauG bootstrap =="

# --- Build root (never inside the repo tree, rule R14) ----------------------
if [[ -z "${LUAUG_BUILD_ROOT:-}" ]]; then
  export LUAUG_BUILD_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/luaug/build"
  echo "LUAUG_BUILD_ROOT defaulting to $LUAUG_BUILD_ROOT"
  echo "(add 'export LUAUG_BUILD_ROOT=$LUAUG_BUILD_ROOT' to your shell profile)"
else
  echo "LUAUG_BUILD_ROOT = $LUAUG_BUILD_ROOT"
fi
case "$LUAUG_BUILD_ROOT" in
  "$repo_root"*) echo "ERROR: LUAUG_BUILD_ROOT must be OUTSIDE the repository tree (rule R14)." >&2; exit 1 ;;
esac
mkdir -p "$LUAUG_BUILD_ROOT"

# --- Toolchain checks -------------------------------------------------------
check() {
  if command -v "$1" >/dev/null 2>&1; then
    echo "  ok  $1 -> $(command -v "$1")"
  else
    echo "  --  $1 not on PATH  ($2)"
    problems+=("$1")
  fi
}
check git    "https://git-scm.com"
check cmake  "CMake >= 3.28 required"
check ninja  "Ninja generator used by the presets"
check clang  "Clang 17+ is the primary Linux compiler"

# --- Pinned Luau toolchain via rokit ---------------------------------------
if command -v rokit >/dev/null 2>&1; then
  # --no-trust-check: tools are pinned in the in-repo, ADR-gated rokit.toml
  # (R5), and the interactive trust prompt cannot be answered in CI.
  (cd "$repo_root" && rokit install --no-trust-check) || problems+=("rokit install")
else
  echo "  --  rokit not on PATH"
  echo "      Install from https://github.com/rojo-rbx/rokit/releases, then run 'rokit self-install'."
  problems+=("rokit")
fi

# --- Lute type definitions --------------------------------------------------
# tools/.luaurc resolves @std / @lute through ~/.lute/typedefs/<version>/.
if command -v lute >/dev/null 2>&1; then
  (cd "$repo_root" && lute setup) || problems+=("lute setup")
elif [[ ! " ${problems[*]-} " =~ " rokit " ]]; then
  problems+=("lute")
fi

echo
echo "Next: vendor third-party sources (idempotent):"
echo "  lute tools/repo/vendor.luau status"
echo "  lute tools/repo/vendor.luau sync"

if [[ ${#problems[@]} -gt 0 ]]; then
  echo
  echo "== bootstrap INCOMPLETE: ${problems[*]} =="
  exit 1
fi

echo
echo "== bootstrap done =="
echo "Next: cmake --preset linux-clang-dev"
