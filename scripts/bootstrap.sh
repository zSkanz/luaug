#!/usr/bin/env bash
# LuauG bootstrap (Linux/macOS). Idempotent; safe to re-run.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

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
    echo "  MISSING: $1  ($2)" >&2
  fi
}
check git    "https://git-scm.com"
check cmake  "CMake >= 3.28 required"
check ninja  "Ninja generator used by the presets"
check rokit  "see https://github.com/rojo-rbx/rokit"

# --- Pinned Luau toolchain via rokit ---------------------------------------
if command -v rokit >/dev/null 2>&1; then
  (cd "$repo_root" && rokit install)
else
  echo "WARNING: rokit not found - lute/luau-lsp/stylua (rokit.toml) not installed." >&2
fi

echo "NOTE: third_party vendor sync activates in M0 (docs/roadmap.md):"
echo "  lute tools/repo/vendor.luau sync"
echo "== bootstrap done =="
