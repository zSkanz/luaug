# The local gate: everything CI checks that can be checked on this machine.
#
# The roadmap already allows gates to run here rather than on a hosted runner
# ("a scripted local gate ... recorded in the gate log either way"). This is
# that script, and it exists for two reasons that are not the same:
#
#   Cost. This repository is private, so Actions minutes carry the platform
#   multipliers and a full run is expensive. Everything below is free.
#
#   Speed. A missing package in a container is a three-minute discovery here and
#   a ten-minute one there -- and the second kind gets batched, which is how a
#   break survives to the next commit.
#
# What it CANNOT do is macOS. Nothing local can, so Tier-3 stays on CI at the
# milestone gate.
#
# Usage:
#   scripts/localgate.ps1              # everything
#   scripts/localgate.ps1 -SkipLinux   # Windows and the static gates only
#   scripts/localgate.ps1 -Only docs   # one stage: docs | luau | windows | linux

[CmdletBinding()]
param(
    [switch]$SkipLinux,
    [ValidateSet('docs', 'luau', 'windows', 'linux')]
    [string]$Only
)

# 'Continue', not 'Stop', and this is not laziness. Windows PowerShell 5.1 turns
# a native command's stderr into error records; under 'Stop' the first line of
# Docker's buildkit progress -- which it writes to stderr on a perfectly healthy
# build -- aborted the stage before anything compiled, and reported the progress
# line itself as the error.
#
# Every stage below therefore checks $LASTEXITCODE and raises its own exception.
# The exit code is the only signal from a native tool that means what it says.
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo

$script:failures = @()
$script:results = @()

function Invoke-Stage {
    param([string]$Name, [scriptblock]$Body)

    if ($Only -and $Only -ne $Name) { return }
    if ($Name -eq 'linux' -and $SkipLinux) { return }

    Write-Host ""
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    $watch = [Diagnostics.Stopwatch]::StartNew()

    # Each stage reports its own failure rather than aborting the run: a gate
    # that stops at the first problem hides the other three, and the point of
    # running locally is to learn everything in one pass.
    #
    # Success is "did not throw", never `$?`. Windows PowerShell 5.1 sets `$?`
    # from whether a native command wrote to stderr, not from its exit code --
    # and Docker's buildkit writes its progress there, so `$?` reported every
    # successful image build as a failure. Each stage below raises explicitly on
    # a non-zero exit code, which is the only signal that means anything here.
    try {
        & $Body
        $ok = $true
    } catch {
        Write-Host $_.Exception.Message -ForegroundColor Red
        $ok = $false
    }
    $watch.Stop()

    $seconds = [math]::Round($watch.Elapsed.TotalSeconds, 1)
    if ($ok) {
        $script:results += "  ok    $Name ($seconds s)"
    } else {
        $script:results += "  FAIL  $Name ($seconds s)"
        $script:failures += $Name
    }
}

# Git Bash, which ships with Git for Windows, runs the same shell scripts CI
# runs. Using the very same files -- not a Windows transcription of them -- is
# what keeps "it passes locally" meaningful.
function Get-BashPath {
    $candidates = @(
        "$env:ProgramFiles\Git\bin\bash.exe",
        "${env:ProgramFiles(x86)}\Git\bin\bash.exe"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    $found = Get-Command bash.exe -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    throw "bash not found. Install Git for Windows, or run with -Only to skip the shell gates."
}

function Get-DeveloperShellEnv {
    # The presets use Ninja with `strategy: external`, so cl, cmake and ninja
    # must already be on PATH -- Visual Studio bundles the last two, and the fix
    # is almost never "install CMake", it is to run vcvars64.bat first.
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; is Visual Studio installed?" }

    $install = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $install) { throw "No Visual Studio with the C++ toolset was found." }

    return Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
}

Invoke-Stage 'docs' {
    & (Get-BashPath) 'scripts/gates/docs-lint.sh'
    if ($LASTEXITCODE -ne 0) { throw "docs-lint failed" }
}

Invoke-Stage 'luau' {
    & (Get-BashPath) 'scripts/gates/luau-check.sh'
    if ($LASTEXITCODE -ne 0) { throw "luau-check failed" }
}

Invoke-Stage 'windows' {
    $vcvars = Get-DeveloperShellEnv
    if (-not $env:LUAUG_BUILD_ROOT) {
        $env:LUAUG_BUILD_ROOT = Join-Path $env:LOCALAPPDATA 'LuauG\build'
    }

    # One cmd invocation, because the environment vcvars64.bat establishes does
    # not survive back into PowerShell.
    $script = @"
call "$vcvars" >nul || exit /b 1
cmake --preset win-msvc-dev || exit /b 1
cmake --build --preset win-msvc-dev || exit /b 1
ctest --preset win-msvc-dev --output-on-failure || exit /b 1
"@
    $temp = Join-Path $env:TEMP "luaug-localgate-$PID.cmd"
    Set-Content -Path $temp -Value $script -Encoding ascii
    try {
        & cmd.exe /c $temp
        if ($LASTEXITCODE -ne 0) { throw "the Windows build or tests failed" }
    } finally {
        Remove-Item $temp -ErrorAction SilentlyContinue
    }
}

Invoke-Stage 'linux' {
    # No stderr redirection on any of these. Windows PowerShell 5.1 wraps a
    # native command's stderr in an ErrorRecord, which under
    # $ErrorActionPreference='Stop' throws even when the exit code is zero --
    # so `docker rm -f <nonexistent>` used to kill this stage in four seconds,
    # before a single object was compiled.
    $server = docker version --format '{{.Server.Version}}'
    if ($LASTEXITCODE -ne 0 -or -not $server) {
        throw "Docker is not running. Start Docker Desktop, or pass -SkipLinux."
    }

    docker build -f scripts/docker/tier2.Dockerfile -t luaug-tier2:latest .
    if ($LASTEXITCODE -ne 0) { throw "the Tier-2 image failed to build" }

    # A named volume, so the second run is incremental. This is the local
    # equivalent of CI's build cache, and it costs nothing.
    docker volume create luaug-tier2-build | Out-Null

    # Asked for by id first: removing a container that is not there is an error
    # on stderr, and see above for why that matters here.
    $existing = docker ps -aq --filter 'name=^luaug-tier2-gate$'
    if ($existing) { docker rm -f luaug-tier2-gate | Out-Null }

    # Named and not --rm, so the container and its full log stay visible in
    # Docker Desktop after it exits -- a disposable container takes its own
    # evidence with it.
    docker run --name luaug-tier2-gate `
        -v "${repo}:/repo" `
        -v "luaug-tier2-build:/build" `
        luaug-tier2:latest bash scripts/gates/linux-build.sh
    if ($LASTEXITCODE -ne 0) { throw "the Tier-2 build or tests failed" }
}

Pop-Location

Write-Host ""
Write-Host "=== local gate ===" -ForegroundColor Cyan
$script:results | ForEach-Object { Write-Host $_ }

if ($script:failures.Count -gt 0) {
    Write-Host ""
    Write-Host "FAILED: $($script:failures -join ', ')" -ForegroundColor Red
    exit 1
}

Write-Host ""
if ($SkipLinux -or $Only) {
    Write-Host "green (partial run -- macOS is Tier-3 and only CI can build it)" -ForegroundColor Yellow
} else {
    Write-Host "green (macOS is Tier-3 and only CI can build it)" -ForegroundColor Green
}
exit 0
