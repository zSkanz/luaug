# The editor's distribution archive (ADR 0054).
#
# Build the two profiles a package needs, write the folder, PROVE it works from
# outside this repository, and only then compress it. The order is the point:
# an archive published without the third step is an archive whose first user is
# the person who finds out it cannot find its own engine.
#
# Usage:
#   scripts\package.ps1                 # build, package, verify, archive
#   scripts\package.ps1 -SkipBuild      # the presets are already built
#   scripts\package.ps1 -NoArchive      # leave the folder, skip the zip
#
# Windows, because this is the tier phase one ships. The packager underneath is
# not Windows-specific -- it packages for whatever platform it runs on -- and
# this script is the part that knows about vcvars, `chcp` and Compress-Archive.

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$NoArchive,
    # Where the folder and the archive go. Defaults to the packager's own
    # default, which is `$env:LUAUG_BUILD_ROOT\package`.
    [string]$Out
)

# 'Continue' rather than 'Stop', for the reason localgate.ps1 gives at length:
# Windows PowerShell 5.1 turns a native command's stderr into error records, and
# under 'Stop' an ordinary progress line aborts the run. $LASTEXITCODE is the
# only signal from a native tool that means what it says.
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
. "$PSScriptRoot/devshell.ps1"

try {
    if (-not $env:LUAUG_BUILD_ROOT) {
        $env:LUAUG_BUILD_ROOT = Join-Path $env:LOCALAPPDATA 'LuauG\build'
    }

    if (-not $SkipBuild) {
        Write-Host "=== build (editor, player) ===" -ForegroundColor Cyan
        $vcvars = Get-DeveloperShellEnv

        # One cmd invocation, because the environment vcvars64.bat establishes
        # does not survive back into PowerShell -- and `chcp 65001` first,
        # because without it ninja records no header dependencies at all and
        # every incremental build reuses stale objects (D040).
        #
        # Named targets rather than the whole tree: what the folder carries is
        # the host, the two tools the CLI shells out to, and the player host.
        # Building the C++ suite on the way would be compiling a hundred
        # translation units this then does not ship, which is the reason the
        # `editor` profile turns them off in the first place.
        $script = @"
chcp 65001 >nul
call "$vcvars" >nul || exit /b 1
cmake --preset win-msvc-editor || exit /b 1
cmake --build --preset win-msvc-editor --target luaug_host assetc iconpatch || exit /b 1
cmake --preset win-msvc-player || exit /b 1
cmake --build --preset win-msvc-player --target luaug_host || exit /b 1
"@
        $temp = Join-Path $env:TEMP "luaug-package-$PID.cmd"
        Set-Content -Path $temp -Value $script -Encoding ascii
        try {
            & cmd.exe /c $temp
            if ($LASTEXITCODE -ne 0) { throw "the editor or player build failed" }
        } finally {
            Remove-Item $temp -ErrorAction SilentlyContinue
        }
    }

    Write-Host "=== the folder ===" -ForegroundColor Cyan
    $arguments = @('tools/repo/package.luau')
    if ($Out) { $arguments += "--out=$Out" }
    & lute $arguments
    if ($LASTEXITCODE -ne 0) { throw "the packager reported missing files" }

    # **Verified before it is compressed.** The suite runs the packaged `luaug`
    # from a scratch directory with no LUAUG_BUILD_ROOT in its environment: if
    # the folder cannot find its own engine, its own template and its own
    # version, there is nowhere else for it to look.
    Write-Host "=== the folder works from outside this repository ===" -ForegroundColor Cyan
    & lute test tests/installed
    if ($LASTEXITCODE -ne 0) { throw "the packaged folder did not pass tests/installed" }

    $root = if ($Out) { $Out } else { Join-Path $env:LUAUG_BUILD_ROOT 'package' }
    $folder = Get-ChildItem -Path $root -Directory -Filter 'LuauG-*' |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $folder) { throw "no LuauG-* folder under $root" }

    if ($NoArchive) {
        Write-Host "package: $($folder.FullName)" -ForegroundColor Green
        return
    }

    Write-Host "=== the archive ===" -ForegroundColor Cyan
    $archive = Join-Path $root "$($folder.Name).zip"
    Remove-Item $archive -ErrorAction SilentlyContinue
    Compress-Archive -Path $folder.FullName -DestinationPath $archive
    $megabytes = [math]::Round((Get-Item $archive).Length / 1MB, 1)
    Write-Host "package: $archive ($megabytes MiB)" -ForegroundColor Green
} finally {
    Pop-Location
}
