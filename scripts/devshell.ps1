# Where the Visual Studio Developer Shell lives on this machine.
#
# Dot-sourced by `localgate.ps1` and by `package.ps1`, because "how do I get a
# compiler on PATH" has one answer and two copies of it would drift the first
# time Visual Studio moved the file -- the same argument `findTool` makes on the
# Luau side.
#
#   . "$PSScriptRoot/devshell.ps1"
#   $vcvars = Get-DeveloperShellEnv

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
