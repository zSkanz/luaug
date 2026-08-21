# LuauG bootstrap (Windows). Idempotent; safe to re-run.
# 1) Ensures LUAUG_BUILD_ROOT (out-of-tree builds, rule R14)
# 2) Checks the native toolchain and points at the Developer Shell if needed
# 3) Installs the pinned Luau toolchain via rokit (rokit.toml)
# 4) Generates Lute type definitions so luau-lsp can resolve @std / @lute
#
# Exits non-zero if anything required is missing. A bootstrap that reports
# success while a step failed is worse than no bootstrap: the failure resurfaces
# later as a confusing build error (and would break the M8 clean-machine gate).

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$problems = @()

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

# --- Native toolchain -------------------------------------------------------
function Test-Tool([string]$name, [string]$hint) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { Write-Host ("  ok  {0} -> {1}" -f $name, $cmd.Source); return $true }
    Write-Host ("  --  {0} not on PATH  ({1})" -f $name, $hint)
    return $false
}

if (-not (Test-Tool git "https://git-scm.com")) { $problems += "git" }

# The presets use the Ninja generator with `strategy: external`, which means the
# MSVC environment must already be set. Visual Studio bundles CMake and Ninja,
# so the usual fix is not "install CMake" but "run from the Developer Shell".
$haveCMake = Test-Tool cmake "CMake >= 3.28 required"
$haveNinja = Test-Tool ninja "Ninja generator used by the presets"

if (-not ($haveCMake -and $haveNinja)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = $null
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    }
    if ($vsPath) {
        Write-Host ""
        Write-Host "Visual Studio found at: $vsPath"
        Write-Host "It bundles CMake and Ninja, but the presets need the MSVC environment."
        Write-Host "Build from a Developer Shell, or prefix commands with:"
        Write-Host "  chcp 65001 && `"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" && cmake --preset win-msvc-dev"
        Write-Host ""
        Write-Host "The chcp is not cosmetic (D040): on a LOCALISED MSVC, ninja cannot match the"
        Write-Host "/showIncludes prefix under any other codepage, records no header dependencies,"
        Write-Host "and every incremental build silently reuses objects built against an old header."
    } else {
        Write-Host "No Visual Studio C++ toolchain found. Install VS with 'Desktop development with C++'."
    }
    $problems += "msvc-environment"
}

# --- Pinned Luau toolchain via rokit ----------------------------------------
if (Get-Command rokit -ErrorAction SilentlyContinue) {
    Push-Location $repoRoot
    try {
        # --no-trust-check: the tools are pinned in rokit.toml, which is
        # reviewed in-repo and ADR-gated (R5). The interactive trust prompt
        # cannot be answered in CI or by an autonomous session.
        & rokit install --no-trust-check
        if ($LASTEXITCODE -ne 0) { $problems += "rokit install" }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "  --  rokit not on PATH"
    Write-Host "      Install it from https://github.com/rojo-rbx/rokit/releases, then run 'rokit self-install'."
    $problems += "rokit"
}

# --- Lute type definitions --------------------------------------------------
# luau-lsp resolves @std / @lute for tools/ through tools/.luaurc, which points
# at ~/.lute/typedefs/<version>/. Without this step `luaug check` cannot resolve
# a single tooling require.
if (Get-Command lute -ErrorAction SilentlyContinue) {
    Push-Location $repoRoot
    try {
        & lute setup
        if ($LASTEXITCODE -ne 0) { $problems += "lute setup" }
    } finally {
        Pop-Location
    }
} elseif ($problems -notcontains "rokit") {
    $problems += "lute"
}

# --- Vendored dependencies --------------------------------------------------
Write-Host ""
Write-Host "Next: vendor third-party sources (idempotent):"
Write-Host "  lute tools/repo/vendor.luau status"
Write-Host "  lute tools/repo/vendor.luau sync"

if ($problems.Count -gt 0) {
    Write-Host ""
    Write-Host "== bootstrap INCOMPLETE: $($problems -join ', ') =="
    exit 1
}

Write-Host ""
Write-Host "== bootstrap done =="
Write-Host "Next: cmake --preset win-msvc-dev  (from a Developer Shell)"
