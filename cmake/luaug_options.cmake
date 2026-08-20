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
option(LUAUG_RHI_SDLGPU "Build the SDL3 GPU render backend (the v1 default)" ON)
option(LUAUG_RHI_NULL "Build the no-op render backend" ON)
option(LUAUG_RHI_CAPTURE "Build the command-stream recording render backend" ON)

# --- Shader toolchain (ADR 0006, ADR 0032) ----------------------------------
# The defaults encode decisions rather than discovering them at build time.
#
# Off when cross-compiling, because shadercross is a HOST tool -- it runs during
# the build to produce blobs, and architecture.md §8 says so explicitly ("builds
# once as a host tool, also used when cross-compiling"). A cross build has no
# business compiling a compiler for its target, and DirectXShaderCompiler
# publishes no artifact for one either, so the nightly Android job would fail at
# configure trying to fetch something that does not exist. Shaders for a
# cross-compiled target come from a host build.
#
# Off on macOS, because Microsoft publishes no macOS DirectXShaderCompiler
# binary at all (ADR 0032 states this consequence). Forcing it ON there is
# allowed and fails with that explanation rather than a confusing not-found.
#
# It stays an option rather than becoming a hard platform rule because turning
# it off is also how a build that only consumes precompiled shader blobs skips
# the whole toolchain.
if(APPLE OR CMAKE_CROSSCOMPILING)
    set(LUAUG_SHADER_TOOLCHAIN_DEFAULT OFF)
else()
    set(LUAUG_SHADER_TOOLCHAIN_DEFAULT ON)
endif()

option(LUAUG_SHADER_TOOLCHAIN
    "Build the HLSL shader toolchain (SDL_shadercross + fetched DirectXShaderCompiler)"
    ${LUAUG_SHADER_TOOLCHAIN_DEFAULT})

# --- Debug UI (ADR 0011) -----------------------------------------------------
# Dear ImGui, dev-facing only and compiled out of shipping builds. Gating the
# TARGET rather than the call sites is what makes "a shipping binary contains no
# ImGui" a checkable claim: there is no object file to find, because none was
# compiled. `app`'s overlay degrades to a class whose methods do nothing, not to
# an #ifdef at every call site.
#
# The second condition is not defensive. The only ImGui renderer backend this
# repository compiles is the SDL_GPU one, so a build without that RHI backend
# has nothing for the overlay to draw with -- and an overlay target that cannot
# render is a link error waiting for whoever switches the option on.
cmake_dependent_option(LUAUG_DEBUG_UI
    "Build the Dear ImGui debug overlay (ADR 0011: dev builds only)" ON
    "NOT LUAUG_PROFILE STREQUAL \"shipping\";LUAUG_RHI_SDLGPU" OFF)

# --- Physics backend selection (ADR 0007, ADR 0023) --------------------------
# The same shape as the RHI options above: `physics_api` is the seam every
# module above L2 sees, and a backend is a build-time choice `app` resolves.
# One backend exists in v1 and the option still exists, because "swappable" is
# a claim the build has to be able to make rather than a promise in an ADR.
option(LUAUG_PHYSICS_JOLT "Build the Jolt physics backend (the v1 default)" ON)

# Jolt's own wireframe output, bridged to the engine's debug draw (roadmap M5).
#
# It is a dependent option and not a plain one because the thing it turns on is
# a chunk of Jolt compiled into the binary: JPH_DEBUG_RENDERER adds virtual
# draw calls to shapes and a renderer base class. A shipping build has no
# business carrying either -- ADR 0011's argument for gating the ImGui TARGET
# rather than its call sites, applied to the same kind of debt.
cmake_dependent_option(LUAUG_PHYSICS_DEBUG_DRAW
    "Bridge Jolt's debug renderer to the engine debug draw (dev builds only)" ON
    "NOT LUAUG_PROFILE STREQUAL \"shipping\";LUAUG_PHYSICS_JOLT" OFF)

set(LUAUG_SANITIZE "" CACHE STRING
    "Comma-separated sanitizer list passed to -fsanitize (e.g. address,undefined)")

if(NOT LUAUG_PROFILE MATCHES "^(debug|dev|profile|shipping)$")
    message(FATAL_ERROR
        "LUAUG_PROFILE must be one of: debug, dev, profile, shipping (got '${LUAUG_PROFILE}')")
endif()

message(STATUS "LuauG: profile=${LUAUG_PROFILE} luau_compiler=${LUAUG_LUAU_COMPILER} tests=${LUAUG_BUILD_TESTS}")
