# LuauG superbuild root.
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

set(LUAUG_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(LUAUG_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(LUAUG_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

# ABI-defining and repo-wide BEFORE any subdirectory (ADR 0013). These are
# already Luau's defaults; stating them makes the decision explicit and, more
# importantly, guarantees that if they are ever changed they change for the VM
# and for our code together. A per-target definition here would be an ABI
# landmine: LUA_VECTOR_DOUBLE alone moves LUA_TVECTOR's ordinal in `lua_Type`.
add_compile_definitions(LUA_VECTOR_SIZE=3 LUA_VECTOR_DOUBLE=0)

include(${LUAUG_CMAKE_DIR}/luaug_options.cmake)
include(${LUAUG_CMAKE_DIR}/luaug_warnings.cmake)
include(${LUAUG_CMAKE_DIR}/luaug_sanitizers.cmake)
include(${LUAUG_CMAKE_DIR}/luaug_provenance.cmake)
include(${LUAUG_CMAKE_DIR}/luaug_module.cmake)

if(LUAUG_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(third_party)

# Modules are added in layer order; luaug_add_module() refuses a dependency it
# has not seen yet, which makes an out-of-order edit a configure error rather
# than a subtle cycle (architecture.md §2).
add_subdirectory(engine/core)
add_subdirectory(engine/platform)
add_subdirectory(engine/app)
