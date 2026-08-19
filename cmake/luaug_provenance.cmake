# Build provenance (ADR 0031).
#
# third_party/manifest.json is the authority on what is pinned (ADR 0021). This
# reads the Luau row at configure time and generates a header carrying the
# version and full commit SHA, so the value `--version` prints is derived from
# the pin rather than duplicated in source where the two could drift.
#
# Luau ships no version constant of its own -- verified across the whole
# vendored tree at 0.734 -- which is why the manifest is the source here and
# why the gate additionally checks header-derived ABI constants (see
# engine/app: those come from the vendored headers at compile time).

set(_luaug_manifest "${LUAUG_THIRD_PARTY_DIR}/manifest.json")

if(NOT EXISTS "${_luaug_manifest}")
    message(FATAL_ERROR "third_party/manifest.json not found -- run: lute tools/repo/vendor.luau sync")
endif()

# Re-configure when the pin changes, so a manifest edit can never leave a stale
# provenance header behind.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_luaug_manifest}")

file(READ "${_luaug_manifest}" _luaug_manifest_text)

# Manifest rows are one JSON object per line, so the row is matched first and
# its fields second -- this cannot accidentally pick up another dependency's
# version the way a whole-file field match could.
string(REGEX MATCH "\"name\"[ \t]*:[ \t]*\"luau\"[^\n]*" _luaug_luau_row "${_luaug_manifest_text}")
if(_luaug_luau_row STREQUAL "")
    message(FATAL_ERROR "third_party/manifest.json has no dependency row named \"luau\"")
endif()

string(REGEX MATCH "\"version\"[ \t]*:[ \t]*\"([^\"]+)\"" _luaug_unused "${_luaug_luau_row}")
set(LUAUG_LUAU_VERSION "${CMAKE_MATCH_1}")

string(REGEX MATCH "\"commit\"[ \t]*:[ \t]*\"([^\"]+)\"" _luaug_unused "${_luaug_luau_row}")
set(LUAUG_LUAU_COMMIT "${CMAKE_MATCH_1}")

# CMake's regex engine has no bounded-repetition operator, so the length is
# checked separately rather than with {40}.
string(LENGTH "${LUAUG_LUAU_COMMIT}" _luaug_commit_length)
if(NOT _luaug_commit_length EQUAL 40 OR NOT LUAUG_LUAU_COMMIT MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR
        "Luau is not pinned to a resolved commit (manifest says '${LUAUG_LUAU_COMMIT}').\n"
        "Resolve it with: lute tools/repo/vendor.luau resolve luau ${LUAUG_LUAU_VERSION}")
endif()

if(NOT EXISTS "${LUAUG_THIRD_PARTY_DIR}/luau/VM/include/lua.h")
    message(FATAL_ERROR
        "Luau is pinned but not vendored -- run: lute tools/repo/vendor.luau sync luau")
endif()

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/build_info.h.in"
    "${LUAUG_GENERATED_DIR}/luaug/core/build_info.h"
    @ONLY)

add_library(luaug_build_info INTERFACE)
add_library(luaug::build_info ALIAS luaug_build_info)
target_include_directories(luaug_build_info INTERFACE "${LUAUG_GENERATED_DIR}")

message(STATUS "LuauG: Luau ${LUAUG_LUAU_VERSION} @ ${LUAUG_LUAU_COMMIT}")
