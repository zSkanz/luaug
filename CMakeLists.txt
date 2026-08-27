# LuauG superbuild root.
#
# Builds are ALWAYS out-of-tree: binaryDir = $env{LUAUG_BUILD_ROOT}/<preset>
# (rule R14). scripts/bootstrap.* sets LUAUG_BUILD_ROOT if unset.

cmake_minimum_required(VERSION 3.28)

project(LuauG
    VERSION 1.1.0
    DESCRIPTION "Standalone game engine scripted in Luau with a familiar Instance/Service developer experience"
    LANGUAGES CXX C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-tree builds are forbidden (rule R14). Use a preset: cmake --preset <name>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C++20 modules are not used anywhere in this repository or in anything it
# vendors, but CMake 3.28+ scans every C++20 translation unit for module
# dependencies regardless -- an extra pass per file, on both tiers, to discover
# nothing.
#
# Turning it off also removes a toolchain requirement that is not uniformly met:
# the scan needs `clang-scan-deps`, and Ubuntu ships it only under a versioned
# name, so a plain `apt-get install clang` produces a compiler that cannot
# build C++20 targets under CMake's default. Found by running the Linux tier in
# a container on the dev machine rather than on a hosted runner.
#
# Reversible in one line if the engine ever adopts modules -- at which point the
# scanner becomes a real requirement rather than a tax.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Profiles: debug | dev | profile | player | shipping (docs/architecture.md §8)
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

# The shader toolchain, before third_party/: luaug_dxc.cmake fetches and
# verifies DirectXShaderCompiler (ADR 0032) and seeds the find results that
# SDL_shadercross resolves during add_subdirectory.
include(${LUAUG_CMAKE_DIR}/luaug_dxc.cmake)
include(${LUAUG_CMAKE_DIR}/luaug_shaders.cmake)

if(LUAUG_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(third_party)

# Modules are added in layer order; luaug_add_module() refuses a dependency it
# has not seen yet, which makes an out-of-order edit a configure error rather
# than a subtle cycle (architecture.md §2).
add_subdirectory(engine/core)
add_subdirectory(engine/jobs)
add_subdirectory(engine/platform)
add_subdirectory(engine/rhi)
add_subdirectory(engine/net)
add_subdirectory(engine/nav)
add_subdirectory(engine/asset)
add_subdirectory(engine/physics)
add_subdirectory(engine/scene)
add_subdirectory(engine/render)
add_subdirectory(engine/replication)
add_subdirectory(engine/audio)
add_subdirectory(engine/input)
add_subdirectory(engine/ui)
add_subdirectory(engine/script)
add_subdirectory(engine/app)

# Repo tooling, after third_party so it can link what it vendors.
add_subdirectory(tools/imgcmp)

# The offline asset compiler, after every engine module it links (M7). It sits
# with the other tools rather than under engine/ because it is not part of the
# runtime: it is what `luaug build-assets` drives.
add_subdirectory(tools/assetc)

# The application-identity tool (M8). Windows-only inside; see its CMakeLists
# for why a Windows icon has no portable writer.
add_subdirectory(tools/iconpatch)

# Standalone native samples, after every module they may link. They consume the
# engine and nothing in the engine consumes them, so they come last.
add_subdirectory(samples)

# Engine shaders (ADR 0006). Attached here rather than in engine/app because the
# shader build belongs to the content pipeline, not to the host module's source
# list -- `luaug_host` is simply the target that must not be considered built
# until its shaders exist.
luaug_add_shaders(luaug_host GLOB "${CMAKE_CURRENT_SOURCE_DIR}/shaders/src/*.hlsl")
