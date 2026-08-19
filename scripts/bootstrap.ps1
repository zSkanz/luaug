# LuauG bootstrap (Windows). Idempotent; safe to re-run.
# 1) Ensures LUAUG_BUILD_ROOT (out-of-tree builds, rule R14)
# 2) Checks required tools; installs the pinned Luau toolchain via rokit
# 3) (From M0) initializes vendored third_party per the manifest

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

Write-Host "== LuauG bootstrap =="

# --- Build root (never inside the repo tree) --------------------------------
if (-not $env:LUAUG_BUILD_ROOT) {
    $default = Join-Path $env:LOCALAPPDATA "LuauG\build"
    [Environment]::SetEnvironmentVariable("LUAUG_BUILD_ROOT", $default, "User")
    $env:LUAUG_BUILD_ROOT = $default
    Write-Host "LUAUG_BUILD_ROOT set to $default (user env var)"
} else {
    Write-Host "LUAUG_BUILD_ROOT = $($env:LUAUG_BUILD_ROOT)"
}
if ($env:LUAUG_BUILD_ROOT.StartsWith($repoRoot)) {
    throw "LUAUG_BUILD_ROOT must be OUTSIDE the repository tree (rule R14)."
}
New-Item -ItemType Directory -Force $env:LUAUG_BUILD_ROOT | Out-Null

# --- Toolchain checks -------------------------------------------------------
function Test-Tool([string]$name, [string]$hint) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { Write-Host ("  ok  {0} -> {1}" -f $name, $cmd.Source) }
    else      { Write-Warning ("missing: {0}  ({1})" -f $name, $hint) }
}
Test-Tool git   "https://git-scm.com"
Test-Tool cmake "CMake >= 3.28 required"
Test-Tool ninja "Ninja generator used by the presets"
Test-Tool rokit "cargo install rokit / see https://github.com/rojo-rbx/rokit"

# --- Pinned Luau toolchain via rokit ---------------------------------------
if (Get-Command rokit -ErrorAction SilentlyContinue) {
    Push-Location $repoRoot
    rokit install
    Pop-Location
} else {
    Write-Warning "rokit not found - lute/luau-lsp/stylua (rokit.toml) were not installed."
}

# --- Vendored dependencies (activated in M0) --------------------------------
# From M0, third_party import runs via the pinned Lute:
#   lute tools/repo/vendor.luau sync
Write-Host "NOTE: third_party vendor sync activates in M0 (docs/roadmap.md)."

Write-Host "== bootstrap done =="
Write-Host "Next: cmake --preset win-msvc-dev   (activates in M0)"
