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
#   scripts/localgate.ps1              # everything -- what you run before a push
#   scripts/localgate.ps1 -Only docs   # one stage: docs | luau | format | windows | linux | shipping
#   scripts/localgate.ps1 -SkipLinux   # ONLY when Docker is genuinely unavailable
#   scripts/localgate.ps1 -Only format -Fix   # rewrite the C++ tree instead of checking it
#
# The Linux stage is about twelve seconds warm and is not redundant with the
# Windows one: Clang diagnoses things MSVC does not, warnings are errors, and it
# has already caught a defect that would otherwise have reached CI. Skipping it
# to go faster is a false economy -- use -Only for that.

[CmdletBinding()]
param(
    [switch]$SkipLinux,
    [ValidateSet('docs', 'luau', 'format', 'windows', 'linux', 'shipping')]
    [string]$Only,
    # Only meaningful with -Only format: reformat in place rather than report.
    # Off by default, because a gate that edits your tree without being asked is
    # not a gate.
    [switch]$Fix
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
    # Both container stages answer to the same switch: -SkipLinux means "Docker
    # is not available here", and the formatting gate runs in that same image
    # because that is where the pinned clang-format lives.
    if (($Name -eq 'linux' -or $Name -eq 'format' -or $Name -eq 'shipping') -and $SkipLinux) { return }

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

# Two stages want the image now -- the formatting gate and the Tier-2 build --
# so it is built once and reused. Docker's layer cache makes the second call
# free; what this avoids is a second copy of the error handling, which is the
# part that was subtle (see the stderr note above).
function Initialize-Tier2Image {
    $server = docker version --format '{{.Server.Version}}'
    if ($LASTEXITCODE -ne 0 -or -not $server) {
        throw "Docker is not running. Start Docker Desktop, or pass -SkipLinux."
    }

    docker build -f scripts/docker/tier2.Dockerfile -t luaug-tier2:latest .
    if ($LASTEXITCODE -ne 0) { throw "the Tier-2 image failed to build" }
}

Invoke-Stage 'docs' {
    & (Get-BashPath) 'scripts/gates/docs-lint.sh'
    if ($LASTEXITCODE -ne 0) { throw "docs-lint failed" }
}

Invoke-Stage 'luau' {
    & (Get-BashPath) 'scripts/gates/luau-check.sh'
    if ($LASTEXITCODE -ne 0) { throw "luau-check failed" }
}

Invoke-Stage 'format' {
    # In the container rather than on this machine, because clang-format's
    # output changes between major versions: Visual Studio ships 20 here, Ubuntu
    # 24.04 -- and therefore both the Tier-2 image and `ubuntu-latest` -- ships
    # 18, and a tree formatted by one is unformatted to the other. The gate
    # script refuses any major but its pin for that reason, so running it on the
    # host would fail against a correctly formatted tree.
    Initialize-Tier2Image

    $arguments = @('scripts/gates/clang-format.sh')
    if ($Fix) { $arguments += '--fix' }

    docker run --rm -v "${repo}:/repo" luaug-tier2:latest bash @arguments
    if ($LASTEXITCODE -ne 0) { throw "the C++ formatting gate failed" }
}

Invoke-Stage 'windows' {
    # A running `luaug-host` holds its own .exe open, so the link fails with
    # LNK1168 -- and the file's timestamp has already moved by then, so the NEXT
    # build considers it current and does not retry. Ninja then reports success
    # and CTest runs a binary from before the change.
    #
    # That cost four wasted cycles and one wrong measurement in M4 before it was
    # understood, which is why this is a guard rather than a note: the orphans
    # come from a run whose output was piped into something that exited early
    # (CLAUDE.md warns about `tail`/`head` for a different symptom of the same
    # thing), and a gate that silently measures yesterday's binary is worse than
    # one that refuses to start.
    $stale = Get-Process -Name 'luaug-host' -ErrorAction SilentlyContinue
    if ($stale) {
        Write-Host "[gate] $($stale.Count) luaug-host process(es) still running; they would hold the executable open." -ForegroundColor Yellow
        $stale | Stop-Process -Force
        Start-Sleep -Milliseconds 200
    }

    $vcvars = Get-DeveloperShellEnv
    if (-not $env:LUAUG_BUILD_ROOT) {
        $env:LUAUG_BUILD_ROOT = Join-Path $env:LOCALAPPDATA 'LuauG\build'
    }

    # One cmd invocation, because the environment vcvars64.bat establishes does
    # not survive back into PowerShell.
    #
    # **`chcp 65001` is load-bearing and not cosmetic** (D040). CMake writes
    # ninja`s `msvc_deps_prefix` -- the string ninja looks for in `/showIncludes`
    # output to learn which headers an object depends on -- as UTF-8. A LOCALISED
    # MSVC emits that string with non-ASCII characters in the console codepage,
    # so under any other codepage the two never match, ninja records NO header
    # dependencies at all, and every incremental build silently reuses objects
    # compiled against an older header. It cost this project four debugging
    # sessions before anybody looked at why.
    $script = @"
chcp 65001 >nul
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

    # The CLI's own path to the same suite. The M3 gate wants `luaug test` green
    # on both tiers, and it is a different path from ctest's: it launches the
    # engine, reads the per-case report and emits TAP. ctest proves the engine;
    # this proves the tool a developer types.
    $tap = Join-Path $env:TEMP "luaug-test-$PID.tap"
    & (Get-BashPath) 'scripts/luaug.sh' test tests/conformance | Set-Content -Path $tap -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "luaug test failed" }
    Get-Content $tap -Tail 1
    Remove-Item $tap -ErrorAction SilentlyContinue

    # The M3 gate's first item: a dev server, this build headless against it, a
    # file mutated by the test, and the reload confirmed over the WebSocket.
    & lute test tests/hotreload
    if ($LASTEXITCODE -ne 0) { throw "the hot-reload gate failed" }

    # M8's packaging chain: `luaug new`, `luaug build`, the built folder RUN with
    # no arguments, and the icon read back out of the artifact. Here rather than
    # in ctest because it drives the CLI, which is a Lute application -- and
    # because `--target win64` is a Windows artifact, which is this stage.
    & lute test tests/packaging
    if ($LASTEXITCODE -ne 0) { throw "the packaging gate failed" }
}

Invoke-Stage 'linux' {
    # No stderr redirection on any of these. Windows PowerShell 5.1 wraps a
    # native command's stderr in an ErrorRecord, which under
    # $ErrorActionPreference='Stop' throws even when the exit code is zero --
    # so `docker rm -f <nonexistent>` used to kill this stage in four seconds,
    # before a single object was compiled.
    Initialize-Tier2Image

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

# The one profile no other stage builds, and therefore the one that spent an
# unknown number of commits not compiling at all (D056). It is here rather than
# folded into the Windows stage for two reasons: the check is a shell script, so
# both callers can run the same file the way CLAUDE.md asks, and Clang with
# warnings-as-errors is the stricter reader of the `#if`s that only this profile
# takes.
#
# Cost: it builds `luaug_host` alone -- the shipping binary, compiled and linked
# -- and not the tree around it. The reasoning is in the script, and the short
# version is that only what LUAUG_LUAU_COMPILER and LUAUG_DEBUG_UI gate can rot
# differently here, and all of it is reachable from that one executable. A few
# seconds warm, one full Release build of the vendored tree cold.
#
# Last, because it is the stage most likely to be cold, and a run that fails
# should say so before spending that.
Invoke-Stage 'shipping' {
    Initialize-Tier2Image
    docker volume create luaug-tier2-build | Out-Null

    $existing = docker ps -aq --filter 'name=^luaug-shipping-gate$'
    if ($existing) { docker rm -f luaug-shipping-gate | Out-Null }

    # The same named volume the Linux stage uses: the two presets write to
    # different subdirectories of it, so the vendored fetches and the ccache-less
    # object trees both survive between runs and neither stage disturbs the
    # other.
    docker run --name luaug-shipping-gate `
        -v "${repo}:/repo" `
        -v "luaug-tier2-build:/build" `
        luaug-tier2:latest bash scripts/gates/shipping-build.sh
    if ($LASTEXITCODE -ne 0) { throw "the shipping profile failed to build" }
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
if ($SkipLinux) {
    # Named rather than folded into a generic "partial", because this is the one
    # skip that hides a whole compiler's diagnostics.
    Write-Host "green, but the Linux tier did not run -- Clang has not seen this change" -ForegroundColor Yellow
} elseif ($Only) {
    Write-Host "green (partial run -- macOS is Tier-3 and only CI can build it)" -ForegroundColor Yellow
} else {
    Write-Host "green (macOS is Tier-3 and only CI can build it)" -ForegroundColor Green
}
exit 0
