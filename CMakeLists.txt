# LuauG superbuild root.
# STATUS: stub — activated in milestone M0 (docs/roadmap.md). The presets in
# CMakePresets.json are final; module wiring lands with M0.
#
# Builds are ALWAYS out-of-tree: binaryDir = $env{LUAUG_BUILD_ROOT}/<preset>
# (rule R14). scripts/bootstrap.* sets LUAUG_BUILD_ROOT if unset.

cmake_minimum_required(VERSION 3.28)

project(LuauG
    VERSION 0.0.1
    DESCRIPTION "Standalone game engine scripted in Luau with a familiar Instance/Service developer experience"
    LANGUAGES CXX C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-tree builds are forbidden (rule R14). Use a preset: cmake --preset <name>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Profiles: debug | dev | profile | shipping (docs/architecture.md §8)
set(LUAUG_PROFILE "dev" CACHE STRING "LuauG build profile")

# --- Activated in M0 -------------------------------------------------------
# include(cmake/luaug_options.cmake)     # LUAUG_RHI_SDLGPU, LUAUG_PHYSICS_JOLT, ... (ADR 0023)
# include(cmake/luaug_warnings.cmake)
# include(cmake/luaug_sanitizers.cmake)
# include(cmake/luaug_luau.cmake)        # vendored Luau: LUA_VECTOR_SIZE=3 (ADR 0013)
# include(cmake/luaug_module.cmake)      # luaug_add_module() with layer enforcement
# include(cmake/luaug_shaders.cmake)
#
# add_subdirectory(third_party)
# add_subdirectory(engine/core)
# add_subdirectory(engine/jobs)
# add_subdirectory(engine/platform)
# ... (layer order per docs/architecture.md §2)
# add_subdirectory(engine/app)
# add_subdirectory(tools/importer)
# add_subdirectory(examples)
# enable_testing()
# ---------------------------------------------------------------------------

message(STATUS "LuauG: docs-first phase — engine targets activate in M0 (docs/roadmap.md).")
