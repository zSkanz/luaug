# Vendored Luau (ADR 0002, ADR 0013).
#
# Luau is added as a SYSTEM subdirectory so its headers never trip our
# warnings-as-errors profile, and with its CLI/tests/web targets off: we embed
# the libraries, we do not ship its tools.
#
# ABI note: LUA_VECTOR_SIZE / LUA_VECTOR_DOUBLE are set repo-wide in the root
# CMakeLists BEFORE this file is included, not per-target. They change the
# layout of every Luau value and the ordinal of LUA_TVECTOR in `lua_Type`
# (docs/research/luau-2026.md §1), so a translation unit compiled with a
# different value than the VM is silently, catastrophically wrong. Repo-wide
# is the only safe scope. C++ code additionally static_asserts them.

set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LUAU_EXTERN_C OFF CACHE BOOL "" FORCE)
set(LUAU_WERROR OFF CACHE BOOL "" FORCE)

# EXCLUDE_FROM_ALL, and it is load-bearing rather than tidiness. Upstream creates
# twelve libraries unconditionally -- third_party/luau/CMakeLists.txt:31-57, with
# options guarding only the CLI, test and web targets (lines 9-15) -- and this
# engine links six: Common, Ast, Bytecode, Compiler, CodeGen, VM. The other six
# were compiled on every build and thrown away, Luau.Analysis worst of all: 407 s
# of the 1178 s of compile time a cold RelWithDebInfo build spent, 35% of it.
# Excluding them took a cold build on twenty cores from 53.9 s to 34.8 s.
#
# Upstream exposes no option to switch Analysis off, so the alternative was
# patching the vendored tree (R13). This does not need one: the `all` target
# stops reaching into Luau, and CMake still builds whatever the six linked
# libraries pull in transitively. The effect is exactly "build what we link and
# nothing else", and it keeps holding when the pin moves and upstream adds,
# renames or splits a library.
add_subdirectory(${LUAUG_THIRD_PARTY_DIR}/luau ${CMAKE_BINARY_DIR}/third_party/luau
                 EXCLUDE_FROM_ALL SYSTEM)

# The engine links these three. Analysis is deliberately absent: type checking
# is `luau-analyze` at the pinned version (ADR 0018), a tool, never a runtime
# dependency. Compiler is gated so shipping builds can drop it and load only
# precompiled bytecode (ADR 0002).
add_library(luaug_luau INTERFACE)
add_library(luaug::luau ALIAS luaug_luau)

target_link_libraries(luaug_luau INTERFACE Luau.VM Luau.CodeGen)

if(LUAUG_LUAU_COMPILER)
    target_link_libraries(luaug_luau INTERFACE Luau.Compiler Luau.Ast)
    target_compile_definitions(luaug_luau INTERFACE LUAUG_LUAU_COMPILER=1)
else()
    target_compile_definitions(luaug_luau INTERFACE LUAUG_LUAU_COMPILER=0)
endif()

# Luau's Bytecode.h lives in Common/include and is not exported by Luau.VM's
# usage requirements; the provenance header (ADR 0031) reads LBC_VERSION_TARGET
# from it.
target_include_directories(luaug_luau SYSTEM INTERFACE
    ${LUAUG_THIRD_PARTY_DIR}/luau/Common/include)
