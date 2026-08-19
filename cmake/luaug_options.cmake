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

set(LUAUG_SANITIZE "" CACHE STRING
    "Comma-separated sanitizer list passed to -fsanitize (e.g. address,undefined)")

if(NOT LUAUG_PROFILE MATCHES "^(debug|dev|profile|shipping)$")
    message(FATAL_ERROR
        "LUAUG_PROFILE must be one of: debug, dev, profile, shipping (got '${LUAUG_PROFILE}')")
endif()

message(STATUS "LuauG: profile=${LUAUG_PROFILE} luau_compiler=${LUAUG_LUAU_COMPILER} tests=${LUAUG_BUILD_TESTS}")
