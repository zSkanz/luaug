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
#   scripts/localgate.ps1 -AllowSkips         # ONLY on a machine with no GPU
#
# **A skipped test is a failure here.** Six gates in this repository answer
# `LUAUG_TEST_SKIP` when there is no graphics device, and ctest counts a skip as
# a pass -- so a GPU that stopped coming up would turn all six green with no
# alarm, and the pixel gates, the two-worlds proof and the settings differential
# would all quietly stop meaning anything. This machine has a device; if a test
# skips on it, something is wrong with the machine or with the test. -AllowSkips
# is for a machine that genuinely has no GPU, and typing it is the point:
# accepting the loss should be a decision, not a default.
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
    [switch]$Fix,
    # Accept skipped tests instead of failing on them. See the note above.
    [switch]$AllowSkips
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

# **A test that skips is a test that did not run, and ctest calls that a pass.**
#
# Six gates here answer `LUAUG_TEST_SKIP` when no graphics device comes up --
# the screenshot golden, the settings differential, the two-worlds editor-seam
# proof and three more. On a machine with a GPU every one of them is supposed to
# execute, so a skip means the device stopped being found and six gates went
# green while measuring nothing. That failure is silent by construction, which is
# exactly the kind this repository writes checks for.
#
# The list of what skipped is printed rather than only counted, because "one
# test skipped" sends somebody looking and "screenshot_gate skipped" tells them
# what they lost.
function Assert-NoSkips {
    param([string]$Log)

    if (-not (Test-Path $Log)) { return }
    $skipped = Select-String -Path $Log -Pattern '^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.+\*+Skipped' -AllMatches |
        ForEach-Object { $_.Matches[0].Groups[1].Value }

    if (-not $skipped -or $skipped.Count -eq 0) { return }

    $names = ($skipped | Sort-Object -Unique) -join ', '
    if ($AllowSkips) {
        Write-Host "[gate] $($skipped.Count) test(s) skipped, accepted by -AllowSkips: $names" -ForegroundColor Yellow
        return
    }

    throw ("$($skipped.Count) test(s) SKIPPED, and ctest counts a skip as a pass: $names`n" +
        "        On a machine with a graphics device these are supposed to run. A skip here means " +
        "the device stopped being found and those gates went green while measuring nothing.`n" +
        "        If this machine genuinely has no GPU, re-run with -AllowSkips and accept that.")
}

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

# `Get-DeveloperShellEnv`, shared with `package.ps1` rather than written twice.
. "$PSScriptRoot/devshell.ps1"

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
    # `--no-tests=error` because an empty suite is not a passing suite: a preset
    # that stopped registering tests would otherwise report success having run
    # nothing, which is the same shape of lie the skip check below exists for.
    $ctestLog = Join-Path $env:TEMP "luaug-localgate-ctest-$PID.txt"
    $script = @"
chcp 65001 >nul
call "$vcvars" >nul || exit /b 1
cmake --preset win-msvc-dev || exit /b 1
cmake --build --preset win-msvc-dev || exit /b 1
ctest --preset win-msvc-dev --output-on-failure --no-tests=error > "$ctestLog" 2>&1 || (type "$ctestLog" & exit /b 1)
type "$ctestLog"
"@
    $temp = Join-Path $env:TEMP "luaug-localgate-$PID.cmd"
    Set-Content -Path $temp -Value $script -Encoding ascii
    try {
        & cmd.exe /c $temp
        if ($LASTEXITCODE -ne 0) { throw "the Windows build or tests failed" }
        Assert-NoSkips -Log $ctestLog
    } finally {
        Remove-Item $temp -ErrorAction SilentlyContinue
        Remove-Item $ctestLog -ErrorAction SilentlyContinue
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
# **It builds two profiles, not one** (D057): `shipping` and `player`, which are
# the two nothing else here compiles. `player` is what `luaug build` packages --
# the Luau compiler ON and the debug overlay OFF, a combination no other profile
# has -- so without this it would be a shipped artifact no gate ever built,
# which is the objection that ruled out shipping the `shipping` profile in the
# first place.
#
# Cost: `luaug_host` alone from each, compiled and linked, and not the tree
# around them. The reasoning is in the script, and the short version is that
# only what LUAUG_LUAU_COMPILER and LUAUG_DEBUG_UI gate can rot differently
# here, and all of it is reachable from those executables. A few seconds warm,
# one full Release build of the vendored tree cold.
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
    if ($LASTEXITCODE -ne 0) { throw "the shipping or player profile failed to build" }
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
