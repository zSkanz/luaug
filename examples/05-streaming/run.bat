@echo off
REM Convenience launcher for this example. The gates invoke luaug-host directly
REM from CMake, so nothing depends on this file -- it exists so a human does not
REM have to remember where an out-of-tree build put the binary (R14).
REM
REM Extra arguments pass through, so the headless forms still work:
REM   run.bat --headless --frames=600 --exit --screenshot=out.png
REM
REM See examples/README.md for the convention and how to add one.

setlocal

if not defined LUAUG_PRESET set "LUAUG_PRESET=win-msvc-dev"

if not defined LUAUG_BUILD_ROOT (
    echo [run] LUAUG_BUILD_ROOT is not set -- run scripts\bootstrap.ps1 once.
    exit /b 1
)

set "LUAUG_HOST=%LUAUG_BUILD_ROOT%\%LUAUG_PRESET%\engine\app\luaug-host.exe"

if not exist "%LUAUG_HOST%" (
    echo [run] luaug-host not found at:
    echo        %LUAUG_HOST%
    echo [run] Build it from a Developer Shell:
    echo        cmake --build --preset %LUAUG_PRESET%
    exit /b 1
)

set "LUAUG_EXAMPLE=%~dp0"
set "LUAUG_EXAMPLE=%LUAUG_EXAMPLE:~0,-1%"

REM This example's world is GENERATED and not committed (M7 brief, Decision 8):
REM the generator is forty lines and the world it writes is a megabyte and a
REM half. Both steps run here when the output is missing, so a fresh clone works
REM without anybody having to read the README first.
if not exist "%LUAUG_EXAMPLE%\content\world" (
    echo [run] generating the world...
    lute "%LUAUG_EXAMPLE%\tools\generate_world.luau" || exit /b 1
)

if not exist "%LUAUG_EXAMPLE%\.luaug\content.chunks.json" (
    echo [run] compiling the world...
    bash "%LUAUG_EXAMPLE%\..\..\scripts\luaug.sh" build-assets "%LUAUG_EXAMPLE%" || exit /b 1
)

"%LUAUG_HOST%" "%LUAUG_EXAMPLE%" %*
