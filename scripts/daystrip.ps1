# Renders the M4.5 deliverable as one image: `tests/screenshots/daystrip`, a
# fixed camera, one frame per three hours of the day, laid side by side.
#
# The point is a claim no single screenshot can carry -- that the sun crosses the
# sky, that shadows lengthen towards evening, and that `Transparency` passes
# through every value rather than switching. A fixed camera is what makes the
# strip readable: with an orbit in it, nobody can say which variable moved the
# shadow.
#
# Every frame comes from a separate host run. `ClockTime` in that scene is a
# function of the tick count, so `--frames=N` selects the hour, and the host
# writes its screenshot at exit.
#
#   scripts/daystrip.ps1                       # 8 frames -> docs/images/daystrip.png
#   scripts/daystrip.ps1 -Frames 12 -Width 640 # denser, smaller
#
# Needs a real GPU: it renders through the default backend rather than through
# `--rhi=capture`, because the thing being looked at is pixels.

[CmdletBinding()]
param(
    [int]$Frames = 8,
    [int]$Width = 480,
    [int]$Height = 270,
    [string]$Out = "docs/images/daystrip.png",
    [string]$Preset = $env:LUAUG_PRESET
)

$ErrorActionPreference = 'Continue'

if (-not $Preset) { $Preset = 'win-msvc-dev' }
if (-not $env:LUAUG_BUILD_ROOT) {
    Write-Error "LUAUG_BUILD_ROOT is not set -- run scripts/bootstrap.ps1 once."
    exit 1
}

$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $env:LUAUG_BUILD_ROOT $Preset
$host_exe = Join-Path $buildDir "engine/app/luaug-host.exe"
$strip_exe = Join-Path $buildDir "tools/imgcmp/imgstrip.exe"
$scene = Join-Path $repo "tests/screenshots/daystrip"

foreach ($tool in @($host_exe, $strip_exe)) {
    if (-not (Test-Path $tool)) {
        Write-Error "not built: $tool`nBuild it from a Developer Shell: cmake --build --preset $Preset"
        exit 1
    }
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) "luaug-daystrip"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Path $work | Out-Null

$shots = @()
for ($index = 0; $index -lt $Frames; $index++) {
    # Frame N of the strip is host run N+1: the scene applies its hour on the
    # boot drain, so one frame is hour zero of the strip rather than nothing.
    $shot = Join-Path $work ("frame-{0:d2}.png" -f $index)
    & $host_exe $scene --headless "--frames=$($index + 1)" --exit `
        "--width=$Width" "--height=$Height" "--screenshot=$shot" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Error "the host exited $LASTEXITCODE rendering frame $index"
        exit 1
    }
    $shots += $shot
}

$outPath = Join-Path $repo $Out
$outDir = Split-Path -Parent $outPath
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

& $strip_exe $outPath @shots
if ($LASTEXITCODE -ne 0) {
    Write-Error "imgstrip exited $LASTEXITCODE"
    exit 1
}

Write-Host "day strip: $Frames frames -> $Out"
