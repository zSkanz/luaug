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

add_subdirectory(${LUAUG_THIRD_PARTY_DIR}/luau ${CMAKE_BINARY_DIR}/third_party/luau SYSTEM)

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
