@echo off
REM Convenience launcher for this example. The gates invoke luaug-host directly
REM from CMake, so nothing depends on this file -- it exists so a human does not
REM have to remember where an out-of-tree build put the binary (R14).
REM
REM Extra arguments pass through:
REM   run.bat --headless --frames=3 --exit --rhi=null
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

REM %~dp0 is this file's own directory, so the example runs from any cwd.
REM This example is the single-file project shape: one file mounted as one
REM Script (api-design.md section 4). It has no window and no renderer.
"%LUAUG_HOST%" "%~dp0boot.luau" %*
