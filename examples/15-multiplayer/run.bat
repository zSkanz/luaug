@echo off
REM Convenience launcher for this example. The gates invoke luaug-host directly
REM from CMake, so nothing depends on this file -- it exists so a human does not
REM have to remember where an out-of-tree build put the binary (R14).
REM
REM Extra arguments pass through, so the headless forms still work:
REM   run.bat --headless --frames=120 --exit --screenshot=out.png
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

REM This example is the project-directory shape: every src/scripts/**/*.luau
REM becomes an entry Script and the rest is reached through require
REM (api-design.md section 4). The host is handed the directory, not a file.
REM
REM %~dp0 is this file's own directory and always ends in a backslash, which
REM would escape the closing quote and corrupt the argument -- so it is stripped
REM before the path is passed.
set "LUAUG_EXAMPLE=%~dp0"
set "LUAUG_EXAMPLE=%LUAUG_EXAMPLE:~0,-1%"

"%LUAUG_HOST%" "%LUAUG_EXAMPLE%" %*
