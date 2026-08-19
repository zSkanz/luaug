# Vendored dependency wiring (ADR 0021).
#
# Every dependency is added SYSTEM so its headers are exempt from our
# warnings-as-errors bar (architecture.md §8), and with its own tests, tools,
# examples and install rules disabled -- we consume libraries, not projects.
#
# Rows exist in manifest.json for dependencies later milestones need; they are
# wired here only once a module actually links them. Adding an add_subdirectory
# for something nothing uses would slow every build for no benefit.

include(${LUAUG_CMAKE_DIR}/luaug_luau.cmake)

if(LUAUG_BUILD_TESTS)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(DOCTEST_WITH_MAIN_IN_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(doctest ${CMAKE_BINARY_DIR}/third_party/doctest SYSTEM)
endif()

# ---------------------------------------------------------------------------
# SDL3 (ADR 0004: the platform layer; ADR 0005: SDL_GPU is the default RHI
# backend). Only the subsystems the engine actually delegates to SDL are built.
#
# The list below is an architecture statement, not a size optimization that
# happens to save time -- each OFF marks a responsibility that belongs to
# another module or another milestone, and turning one back ON is a decision,
# not a tweak:
#
#   Audio    -- miniaudio owns audio (ADR 0009); SDL's audio stack is never used.
#   Render   -- SDL's 2D renderer is a rival to our own RHI (ADR 0005).
#   Joystick, Haptic, Hidapi, Sensor
#            -- gamepads belong to the Input Action System in M6 (ADR 0029).
#              Together they are the single largest block of SDL source after
#              video, so they stay off until the milestone that needs them.
#   Camera, Dialog, Tray, Power
#            -- outside the v1 scope (R15).
#
# Kept ON deliberately: the offscreen and dummy video drivers, which are how
# `--headless` gets a GPU device on a machine with no display (roadmap M1).
# ---------------------------------------------------------------------------
set(SDL_SHARED        OFF CACHE BOOL "" FORCE)  # static only: consoles and iOS
set(SDL_STATIC        ON  CACHE BOOL "" FORCE)  # want one dead-strippable binary (ADR 0023)
set(SDL_TEST_LIBRARY  OFF CACHE BOOL "" FORCE)
set(SDL_TESTS         OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES      OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL       OFF CACHE BOOL "" FORCE)
set(SDL_UNINSTALL     OFF CACHE BOOL "" FORCE)

set(SDL_VIDEO         ON  CACHE BOOL "" FORCE)
set(SDL_GPU           ON  CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN     ON  CACHE BOOL "" FORCE)

set(SDL_AUDIO         OFF CACHE BOOL "" FORCE)
set(SDL_RENDER        OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK      OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC        OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI        OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR        OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA        OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG        OFF CACHE BOOL "" FORCE)
set(SDL_TRAY          OFF CACHE BOOL "" FORCE)
set(SDL_POWER         OFF CACHE BOOL "" FORCE)

add_subdirectory(sdl3 ${CMAKE_BINARY_DIR}/third_party/sdl3 SYSTEM)
