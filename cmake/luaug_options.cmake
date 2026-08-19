# Build options (ADR 0023: backends are chosen at build time, wired by one
# explicit factory in `app` -- no plugin ABI, no self-registering statics).
#
# Only options with a consumer today are declared. Backend toggles
# (LUAUG_RHI_SDLGPU, LUAUG_PHYSICS_JOLT, ...) arrive with their modules rather
# than sitting here inert: an option nothing reads is a lie about what the
# build can do.

include(CMakeDependentOption)

option(LUAUG_BUILD_TESTS "Build C++ unit and integration tests" ON)

# The Luau compiler is present in every profile except shipping, which loads
# precompiled bytecode only (ADR 0002). Expressed as a dependent option so the
# shipping profile cannot accidentally carry it.
cmake_dependent_option(LUAUG_LUAU_COMPILER
    "Link the Luau compiler (source -> bytecode at runtime)" ON
    "NOT LUAUG_PROFILE STREQUAL \"shipping\"" OFF)

# --- Backend selection (ADR 0023) -------------------------------------------
# Chosen at build time; `app` holds the one hand-written switch over what was
# compiled in. A shipping build carries exactly one real backend per seam.
#
# `rhi_null` is not a debugging convenience: headless logic tests and the future
# dedicated server need an IDevice that renders nothing, so it is part of the
# normal build rather than something a preset turns on.
# SDLGPU defaults OFF only because its sources do not exist yet; it flips to ON
# in the commit that adds it. An option that can be switched on to produce a
# link error would be worse than no option at all.
option(LUAUG_RHI_NULL "Build the no-op render backend" ON)
option(LUAUG_RHI_CAPTURE "Build the command-stream recording render backend" ON)
option(LUAUG_RHI_SDLGPU "Build the SDL3 GPU render backend (the v1 default)" OFF)

set(LUAUG_SANITIZE "" CACHE STRING
    "Comma-separated sanitizer list passed to -fsanitize (e.g. address,undefined)")

if(NOT LUAUG_PROFILE MATCHES "^(debug|dev|profile|shipping)$")
    message(FATAL_ERROR
        "LUAUG_PROFILE must be one of: debug, dev, profile, shipping (got '${LUAUG_PROFILE}')")
endif()

message(STATUS "LuauG: profile=${LUAUG_PROFILE} luau_compiler=${LUAUG_LUAU_COMPILER} tests=${LUAUG_BUILD_TESTS}")
