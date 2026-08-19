# DirectXShaderCompiler: fetched at configure time, hash-pinned, never
# committed (ADR 0032).
#
# DXC is SDL_shadercross's only HLSL front-end (ADR 0006). It is the one
# dependency that is a binary release artifact rather than a vendored source
# tree, because its source is a fork of LLVM and its archives carry an
# unassigned Microsoft EULA -- ADR 0032 has the full reasoning.
#
# `third_party/manifest.json` stays the single pin authority (R5): this file
# reads the `dxc` row out of it and never restates a URL or a hash, because two
# copies of a hash are two things that can drift.
#
# The hash IS the pin. A mismatch is a supply-chain event, so it is a hard
# error with no "update the hash to whatever we got" path -- changing a hash is
# a human edit to the manifest, exactly as changing a commit SHA is.

# Set unconditionally so `luaug_add_shaders()` and third_party/CMakeLists.txt
# have one variable to branch on rather than re-deriving the platform rule.
set(LUAUG_DXC_RUNTIME_FILES "")

if(NOT LUAUG_SHADER_TOOLCHAIN)
    message(STATUS
        "LuauG: shader toolchain disabled -- no HLSL is compiled by this build (ADR 0032)")
    return()
endif()

# ---------------------------------------------------------------------------
# Manifest row
# ---------------------------------------------------------------------------

set(_luaug_manifest "${LUAUG_THIRD_PARTY_DIR}/manifest.json")

if(NOT EXISTS "${_luaug_manifest}")
    message(FATAL_ERROR "third_party/manifest.json not found -- run: lute tools/repo/vendor.luau sync")
endif()

# Re-configure when the pin changes, so a manifest edit can never leave a build
# pointing at the previously fetched archive.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_luaug_manifest}")

file(READ "${_luaug_manifest}" _luaug_manifest_text)

# Binary rows are read with string(JSON) rather than the line-oriented regex the
# provenance module uses for dependency rows: a binary row is a nested object
# with a per-platform artifact array, and a regex over that would be guesswork.
string(JSON _luaug_binaries_count ERROR_VARIABLE _luaug_json_error
    LENGTH "${_luaug_manifest_text}" binaries)
if(NOT _luaug_json_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR
        "third_party/manifest.json has no readable \"binaries\" array (${_luaug_json_error}).\n"
        "DirectXShaderCompiler is pinned there as a \"kind\": \"binary\" row (ADR 0032).")
endif()
if(_luaug_binaries_count EQUAL 0)
    message(FATAL_ERROR "third_party/manifest.json has an empty \"binaries\" array (ADR 0032)")
endif()

set(_luaug_dxc_row "")
math(EXPR _luaug_binaries_last "${_luaug_binaries_count} - 1")
foreach(_luaug_i RANGE ${_luaug_binaries_last})
    string(JSON _luaug_row GET "${_luaug_manifest_text}" binaries ${_luaug_i})
    string(JSON _luaug_row_name GET "${_luaug_row}" name)
    if(_luaug_row_name STREQUAL "dxc")
        set(_luaug_dxc_row "${_luaug_row}")
        break()
    endif()
endforeach()

if(_luaug_dxc_row STREQUAL "")
    message(FATAL_ERROR "third_party/manifest.json has no binary row named \"dxc\" (ADR 0032)")
endif()

string(JSON LUAUG_DXC_VERSION GET "${_luaug_dxc_row}" version)

# ---------------------------------------------------------------------------
# Host artifact selection
# ---------------------------------------------------------------------------

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _luaug_dxc_cpu)
if(_luaug_dxc_cpu MATCHES "^(amd64|x86_64|x64)$")
    set(_luaug_dxc_arch "x64")
elseif(_luaug_dxc_cpu MATCHES "^(arm64|aarch64)$")
    set(_luaug_dxc_arch "arm64")
elseif(_luaug_dxc_cpu MATCHES "^(x86|i[3-6]86)$")
    set(_luaug_dxc_arch "x86")
else()
    set(_luaug_dxc_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(APPLE)
    # Not "not found": there is nothing to find and there never will be.
    message(FATAL_ERROR
        "There is no macOS DirectXShaderCompiler binary. Microsoft publishes none, "
        "which is why SDL_shadercross's own CI builds DXC from source on its macOS "
        "runner alone.\n"
        "ADR 0032 states the consequence: a developer working on macOS gets no local "
        "shader compilation and therefore no shader hot reload. Shaders for a macOS "
        "build are produced on a Tier-1 or Tier-2 host (architecture.md §8 builds "
        "shadercross as a host tool for exactly this).\n"
        "Configure with -DLUAUG_SHADER_TOOLCHAIN=OFF (the default on this platform) "
        "to build everything else.")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND _luaug_dxc_arch STREQUAL "x64")
    set(_luaug_dxc_platform "windows-x64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _luaug_dxc_arch STREQUAL "x64")
    set(_luaug_dxc_platform "linux-x64")
else()
    message(FATAL_ERROR
        "No pinned DirectXShaderCompiler artifact for ${CMAKE_SYSTEM_NAME}/${_luaug_dxc_arch}.\n"
        "Artifacts are pinned per platform in the \"dxc\" binary row of "
        "third_party/manifest.json; adding one is a human-approved manifest edit (R5).")
endif()

string(JSON _luaug_artifacts_count LENGTH "${_luaug_dxc_row}" artifacts)
set(_luaug_dxc_artifact "")
if(_luaug_artifacts_count GREATER 0)
    math(EXPR _luaug_artifacts_last "${_luaug_artifacts_count} - 1")
    foreach(_luaug_i RANGE ${_luaug_artifacts_last})
        string(JSON _luaug_artifact GET "${_luaug_dxc_row}" artifacts ${_luaug_i})
        string(JSON _luaug_artifact_platform GET "${_luaug_artifact}" platform)
        if(_luaug_artifact_platform STREQUAL _luaug_dxc_platform)
            set(_luaug_dxc_artifact "${_luaug_artifact}")
            break()
        endif()
    endforeach()
endif()

if(_luaug_dxc_artifact STREQUAL "")
    message(FATAL_ERROR
        "The \"dxc\" binary row in third_party/manifest.json has no artifact for "
        "platform \"${_luaug_dxc_platform}\" (R5).")
endif()

string(JSON _luaug_dxc_url GET "${_luaug_dxc_artifact}" url)
string(JSON _luaug_dxc_sha256 GET "${_luaug_dxc_artifact}" sha256)
string(JSON _luaug_dxc_bytes GET "${_luaug_dxc_artifact}" bytes)

# ---------------------------------------------------------------------------
# Fetch, verify, extract
# ---------------------------------------------------------------------------

# ADR 0032 rule 2: the cache lives under $LUAUG_BUILD_ROOT, not in the source
# tree (R14) and not in the per-preset binary dir, so a second preset, a second
# clone and an offline machine all reuse the one verified copy.
if(NOT DEFINED ENV{LUAUG_BUILD_ROOT} OR "$ENV{LUAUG_BUILD_ROOT}" STREQUAL "")
    message(FATAL_ERROR
        "LUAUG_BUILD_ROOT is not set, so there is nowhere to cache the pinned "
        "DirectXShaderCompiler archive (ADR 0032, R14).\n"
        "Run scripts/bootstrap.ps1 (Windows) or ./scripts/bootstrap.sh first.")
endif()

file(TO_CMAKE_PATH "$ENV{LUAUG_BUILD_ROOT}" _luaug_build_root)
set(LUAUG_DXC_CACHE_DIR
    "${_luaug_build_root}/toolchains/dxc/${LUAUG_DXC_VERSION}/${_luaug_dxc_platform}")

# Wrapped in a function purely so `file(LOCK ... GUARD FUNCTION)` has a scope to
# release on. The lock is not decoration: presets share this cache by design, so
# two configures racing on the same archive is the expected case, not an edge.
function(_luaug_fetch_dxc cache url sha256 bytes out_root)
    get_filename_component(archive_name "${url}" NAME)
    set(archive "${cache}/${archive_name}")
    set(partial "${archive}.part")
    set(root "${cache}/root")
    set(stamp "${cache}/extracted.sha256")

    file(MAKE_DIRECTORY "${cache}")
    file(LOCK "${cache}/.lock" GUARD FUNCTION TIMEOUT 1800)

    if(EXISTS "${archive}")
        # Re-verified on every configure rather than trusted from a stamp. The
        # check costs milliseconds and is the only thing standing between a
        # tampered cache and the compiler we run.
        file(SIZE "${archive}" cached_bytes)
        file(SHA256 "${archive}" cached_sha256)
        if(NOT "${cached_bytes}" EQUAL "${bytes}" OR NOT "${cached_sha256}" STREQUAL "${sha256}")
            message(FATAL_ERROR
                "Cached DirectXShaderCompiler archive does not match its pin.\n"
                "  file:     ${archive}\n"
                "  expected: ${sha256} (${bytes} bytes)\n"
                "  actual:   ${cached_sha256} (${cached_bytes} bytes)\n"
                "This is a supply-chain event, not a stale cache to paper over. "
                "Delete the file above and re-configure to re-fetch from the pinned "
                "URL; if the freshly downloaded archive also mismatches, the pin in "
                "third_party/manifest.json is what needs a human (ADR 0032).")
        endif()
    else()
        message(STATUS "LuauG: fetching DirectXShaderCompiler ${url}")
        file(REMOVE "${partial}")
        file(DOWNLOAD "${url}" "${partial}" STATUS download_status LOG download_log TLS_VERIFY ON)
        list(GET download_status 0 download_code)
        if(NOT download_code EQUAL 0)
            list(GET download_status 1 download_message)
            file(REMOVE "${partial}")
            message(FATAL_ERROR
                "Failed to download the pinned DirectXShaderCompiler archive.\n"
                "  url:   ${url}\n"
                "  error: ${download_message}\n"
                "${download_log}")
        endif()

        file(SIZE "${partial}" got_bytes)
        file(SHA256 "${partial}" got_sha256)
        if(NOT "${got_bytes}" EQUAL "${bytes}" OR NOT "${got_sha256}" STREQUAL "${sha256}")
            file(REMOVE "${partial}")
            message(FATAL_ERROR
                "Downloaded DirectXShaderCompiler archive does not match its pin.\n"
                "  url:      ${url}\n"
                "  expected: ${sha256} (${bytes} bytes)\n"
                "  actual:   ${got_sha256} (${got_bytes} bytes)\n"
                "The download was discarded. The hash is the pin (ADR 0032): there is "
                "no path here that adopts whatever arrived. Either the release was "
                "re-cut upstream or the transfer was tampered with, and both need a "
                "human to edit third_party/manifest.json.")
        endif()
        file(RENAME "${partial}" "${archive}")
    endif()

    # The stamp records which archive the extracted tree came from, so a manifest
    # bump re-extracts and a plain re-configure does not.
    set(extracted_from "")
    if(EXISTS "${stamp}" AND IS_DIRECTORY "${root}")
        file(READ "${stamp}" extracted_from)
        string(STRIP "${extracted_from}" extracted_from)
    endif()
    if(NOT "${extracted_from}" STREQUAL "${sha256}")
        message(STATUS "LuauG: extracting ${archive_name}")
        file(REMOVE_RECURSE "${root}")
        file(MAKE_DIRECTORY "${root}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${root}")
        file(WRITE "${stamp}" "${sha256}")
    endif()

    set(${out_root} "${root}" PARENT_SCOPE)
endfunction()

_luaug_fetch_dxc(
    "${LUAUG_DXC_CACHE_DIR}"
    "${_luaug_dxc_url}"
    "${_luaug_dxc_sha256}"
    "${_luaug_dxc_bytes}"
    LUAUG_DXC_ROOT)

# ---------------------------------------------------------------------------
# Hand the extracted tree to SDL_shadercross's own find module
# ---------------------------------------------------------------------------
#
# third_party/sdl_shadercross/CMakeLists.txt:163 assigns
# `DirectXShaderCompiler_ROOT` to its own `external/` path as a *normal*
# variable immediately before `find_package(DirectXShaderCompiler REQUIRED)`,
# which shadows any cache entry we could pass in -- so pointing that variable at
# our cache does not work from outside. What does work is the layer below it:
# its FindDirectXShaderCompiler.cmake resolves everything through find_path /
# find_file / find_library, and those return immediately when their result cache
# variable is already set. So we do the search ourselves, against our root only,
# and seed exactly the variables that module reads.
#
# NO_DEFAULT_PATH is load-bearing rather than an optimization: without it a
# stray dxcompiler.dll on PATH could quietly become the compiler this build
# uses, which would defeat the pin entirely.

if(NOT DEFINED CACHE{LUAUG_DXC_LOCATED_IN} OR NOT "$CACHE{LUAUG_DXC_LOCATED_IN}" STREQUAL "${LUAUG_DXC_ROOT}")
    unset(DirectXShaderCompiler_INCLUDE_PATH CACHE)
    unset(DirectXShaderCompiler_dxcompiler_BINARY CACHE)
    unset(DirectXShaderCompiler_dxcompiler_LIBRARY CACHE)
    unset(DirectXShaderCompiler_dxil_BINARY CACHE)
    unset(DirectXShaderCompiler_dxil_LIBRARY CACHE)
endif()
set(LUAUG_DXC_LOCATED_IN "${LUAUG_DXC_ROOT}" CACHE INTERNAL "DXC root the cached find results belong to")

# Suffix lists mirror FindDirectXShaderCompiler.cmake's, so a repackaged release
# is tolerated here exactly as far as it is tolerated there.
find_path(DirectXShaderCompiler_INCLUDE_PATH
    NAMES "dxcapi.h"
    PATHS "${LUAUG_DXC_ROOT}"
    PATH_SUFFIXES "inc" "include/dxc" "include" "windows/inc" "linux/include/dxc" "linux/include"
    NO_DEFAULT_PATH)

set(_luaug_dxc_required DirectXShaderCompiler_INCLUDE_PATH)

if(WIN32)
    find_file(DirectXShaderCompiler_dxcompiler_BINARY
        NAMES "dxcompiler.dll"
        PATHS "${LUAUG_DXC_ROOT}"
        PATH_SUFFIXES "bin/${_luaug_dxc_arch}" "bin" "windows/bin/${_luaug_dxc_arch}"
        NO_DEFAULT_PATH)
    find_library(DirectXShaderCompiler_dxcompiler_LIBRARY
        NAMES "dxcompiler"
        PATHS "${LUAUG_DXC_ROOT}"
        PATH_SUFFIXES "lib/${_luaug_dxc_arch}" "lib" "windows/lib/${_luaug_dxc_arch}"
        NO_DEFAULT_PATH)
    # Never linked; it is a runtime artifact. dxcompiler.dll loads it
    # dynamically to sign the DXIL it emits, and unsigned DXIL is rejected by
    # D3D12 outside Developer Mode -- so it has to travel with dxcompiler.dll.
    find_file(DirectXShaderCompiler_dxil_BINARY
        NAMES "dxil.dll"
        PATHS "${LUAUG_DXC_ROOT}"
        PATH_SUFFIXES "bin/${_luaug_dxc_arch}" "bin" "windows/bin/${_luaug_dxc_arch}"
        NO_DEFAULT_PATH)
    list(APPEND _luaug_dxc_required
        DirectXShaderCompiler_dxcompiler_BINARY
        DirectXShaderCompiler_dxcompiler_LIBRARY
        DirectXShaderCompiler_dxil_BINARY)
    set(LUAUG_DXC_RUNTIME_FILES
        "${DirectXShaderCompiler_dxcompiler_BINARY}"
        "${DirectXShaderCompiler_dxil_BINARY}")
else()
    find_library(DirectXShaderCompiler_dxcompiler_LIBRARY
        NAMES "dxcompiler"
        PATHS "${LUAUG_DXC_ROOT}"
        PATH_SUFFIXES "lib" "linux/lib"
        NO_DEFAULT_PATH)
    find_library(DirectXShaderCompiler_dxil_LIBRARY
        NAMES "dxil"
        PATHS "${LUAUG_DXC_ROOT}"
        PATH_SUFFIXES "lib" "linux/lib"
        NO_DEFAULT_PATH)
    list(APPEND _luaug_dxc_required
        DirectXShaderCompiler_dxcompiler_LIBRARY
        DirectXShaderCompiler_dxil_LIBRARY)
    # Left empty deliberately: the imported dxcompiler target carries an absolute
    # path in the cache, so CMake's build-tree RPATH already resolves it, and
    # libdxil.so sits in the same directory for dxcompiler's own dlopen.
endif()

foreach(_luaug_var IN LISTS _luaug_dxc_required)
    if(NOT ${_luaug_var})
        message(FATAL_ERROR
            "The pinned DirectXShaderCompiler archive does not have the expected layout: "
            "${_luaug_var} was not found under ${LUAUG_DXC_ROOT}.\n"
            "The archive verified against its SHA256, so this is a repackaged release "
            "rather than a corrupt download -- the pin in third_party/manifest.json and "
            "the suffix lists in cmake/luaug_dxc.cmake need a human (ADR 0032).")
    endif()
endforeach()

message(STATUS "LuauG: DirectXShaderCompiler ${LUAUG_DXC_VERSION} (${_luaug_dxc_platform}) at ${LUAUG_DXC_ROOT}")
