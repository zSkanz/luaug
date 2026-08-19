# Android cross-compile toolchain -- nightly, compile-only
# (architecture.md §8; roadmap "Early Android portability check").
#
# Mobile is post-v1 (R15). This file exists for one reason: so that a toolchain
# or API break in `platform` / `rhi_api` / `rhi_sdlgpu` is found the night it
# lands instead of at the M4 device checkpoint, when the RHI interface is about
# to freeze. Nothing here is a step toward shipping on Android.
#
# It is a thin wrapper over the NDK's own android.toolchain.cmake rather than a
# hand-written toolchain, because the NDK owns the mapping from ABI and API
# level to triple, sysroot and link flags -- a local copy of that mapping rots
# silently with the next NDK release and the symptom is a miscompile, not an
# error. What lives here is the part the NDK does not own: the pins (R5), and
# the check that the NDK actually used is the pinned one.

# The single pin. .github/workflows/nightly.yml reads this literal back out of
# this file to decide which NDK to install, so the repository contains one
# version string rather than two that can drift apart -- and the revision
# assertion below turns any drift into a configure error instead of a build
# against a different compiler than the one named here.
set(LUAUG_ANDROID_NDK_VERSION "27.2.12479018" CACHE STRING
    "Pinned Android NDK package revision (r27c, the LTS line)")

# 21 is SDL's documented minimum for Android
# (third_party/sdl3/docs/README-android.md, "Minimum API level supported by
# SDL: 21"). Naming SDL's floor rather than a higher number keeps this job
# honest about what the vendored code actually claims to support.
set(ANDROID_PLATFORM "android-21" CACHE STRING "Minimum Android API level")

# arm64 only. armeabi-v7a and the two x86 ABIs would triple the nightly's cost
# to cover configurations no target device uses; a second ABI is a decision for
# whoever schedules the device checkpoint, not a default.
set(ANDROID_ABI "arm64-v8a" CACHE STRING "Android ABI to cross-compile for")

# Static, for the same reason SDL is static on the desktop (third_party's
# SDL_STATIC comment): one dead-strippable binary, no runtime search path to get
# wrong.
set(ANDROID_STL "c++_static" CACHE STRING "NDK C++ runtime")

# Resolution order: an explicit -D, then the variables the NDK's own installers
# and the GitHub runner images export, then the SDK's conventional layout. The
# pinned version is part of the last two paths on purpose -- an SDK holding
# three NDKs must still resolve to the pinned one, never to whatever
# ANDROID_NDK_LATEST_HOME happens to point at.
if(NOT LUAUG_ANDROID_NDK_ROOT)
    foreach(_luaug_ndk_candidate IN ITEMS
            "$ENV{ANDROID_NDK_ROOT}"
            "$ENV{ANDROID_NDK_HOME}"
            "$ENV{ANDROID_SDK_ROOT}/ndk/${LUAUG_ANDROID_NDK_VERSION}"
            "$ENV{ANDROID_HOME}/ndk/${LUAUG_ANDROID_NDK_VERSION}")
        if(EXISTS "${_luaug_ndk_candidate}/build/cmake/android.toolchain.cmake")
            set(LUAUG_ANDROID_NDK_ROOT "${_luaug_ndk_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT LUAUG_ANDROID_NDK_ROOT)
    message(FATAL_ERROR
        "Android NDK ${LUAUG_ANDROID_NDK_VERSION} not found.\n"
        "Install it with: sdkmanager --install \"ndk;${LUAUG_ANDROID_NDK_VERSION}\"\n"
        "or point at an existing one: -DLUAUG_ANDROID_NDK_ROOT=<path>")
endif()

# A pin that only names a version and then builds with whichever NDK was on the
# machine is decoration. source.properties is the NDK's own statement of what it
# is, so it is read back and compared.
set(_luaug_ndk_props "${LUAUG_ANDROID_NDK_ROOT}/source.properties")
if(NOT EXISTS "${_luaug_ndk_props}")
    message(FATAL_ERROR
        "'${LUAUG_ANDROID_NDK_ROOT}' has no source.properties -- it is not an NDK.")
endif()

file(READ "${_luaug_ndk_props}" _luaug_ndk_props_text)
string(REGEX MATCH "Pkg\\.Revision[ \t]*=[ \t]*([^\r\n]+)" _luaug_unused "${_luaug_ndk_props_text}")
string(STRIP "${CMAKE_MATCH_1}" _luaug_ndk_revision)

if(NOT _luaug_ndk_revision STREQUAL LUAUG_ANDROID_NDK_VERSION)
    message(FATAL_ERROR
        "Android NDK pin mismatch (R5): expected ${LUAUG_ANDROID_NDK_VERSION}, "
        "'${LUAUG_ANDROID_NDK_ROOT}' reports ${_luaug_ndk_revision}.\n"
        "Change the pin in this file deliberately, or select the pinned NDK with "
        "-DLUAUG_ANDROID_NDK_ROOT=<path>.")
endif()

# try_compile() re-runs this file against an empty cache, so a location that was
# passed with -D would otherwise be recomputed from the environment alone --
# exactly the case where -D was needed. The NDK's own toolchain appends its
# ANDROID_* variables here for the same reason.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    LUAUG_ANDROID_NDK_ROOT
    LUAUG_ANDROID_NDK_VERSION)

include("${LUAUG_ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake")
