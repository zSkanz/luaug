# Vendored dependency wiring (ADR 0021).
#
# Every dependency is added SYSTEM so its headers are exempt from our
# warnings-as-errors bar (architecture.md §8), and with its own tests, tools,
# examples and install rules disabled -- we consume libraries, not projects.
#
# Rows exist in manifest.json for dependencies later milestones need; they are
# wired here only once a module actually links them. Adding an add_subdirectory
# for something nothing uses would slow every build for no benefit.

# ---------------------------------------------------------------------------
# A vendored tree must not discover THIS repository's git metadata.
#
# Several upstream projects run `git describe` during configure to stamp their
# own version. A vendored tree has no `.git` of its own (the vendor tool checks
# out a work tree and leaves the git dir behind on purpose), so git walks up and
# finds ours -- and SPIRV-Cross was baking `milestone/m0-26-g41f57c7` into its
# gitversion.h as though that were a SPIRV-Cross version. That is simply wrong
# information compiled into a binary.
#
# It is also expensive, because `--dirty` forces a full `git status` over the
# whole repository -- and this one carries thousands of vendored files. Measured
# on the same command: 0.71 s natively, and **37 seconds** inside the Linux
# container, where every stat crosses Docker's filesystem bridge. That was about
# 70% of a 53-second configure.
#
# The ceiling stops git before it can ascend out of third_party/, so a vendored
# project correctly concludes it is not in a repository. Set here rather than
# patched into each upstream (R13, ADR 0021): it is one line, it applies to
# every vendored tree present and future, and it survives a pin bump.
set(LUAUG_SAVED_GIT_CEILING "$ENV{GIT_CEILING_DIRECTORIES}")
set(ENV{GIT_CEILING_DIRECTORIES} "${CMAKE_CURRENT_SOURCE_DIR}")

include(${LUAUG_CMAKE_DIR}/luaug_luau.cmake)

if(LUAUG_BUILD_TESTS)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(DOCTEST_WITH_MAIN_IN_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(doctest ${CMAKE_BINARY_DIR}/third_party/doctest SYSTEM)
endif()

# ---------------------------------------------------------------------------
# SDL3 (ADR 0004: the platform layer; ADR 0005: SDL_GPU is the default RHI
# backend). Only the subsystems the engine actually delegates to SDL are built.
#
# The list below is an architecture statement, not a size optimization that
# happens to save time -- each OFF marks a responsibility that belongs to
# another module or another milestone, and turning one back ON is a decision,
# not a tweak:
#
#   Audio    -- miniaudio owns audio (ADR 0009); SDL's audio stack is never used.
#   Render   -- SDL's 2D renderer is a rival to our own RHI (ADR 0005).
#   Joystick, Haptic, Hidapi, Sensor
#            -- gamepads belong to the Input Action System in M6 (ADR 0029).
#              Together they are the single largest block of SDL source after
#              video, so they stay off until the milestone that needs them.
#   Camera, Dialog, Tray, Power
#            -- outside the v1 scope (R15).
#
# Kept ON deliberately: the offscreen and dummy video drivers, which are how
# `--headless` gets a GPU device on a machine with no display (roadmap M1).
# ---------------------------------------------------------------------------
set(SDL_SHARED        OFF CACHE BOOL "" FORCE)  # static only: consoles and iOS
set(SDL_STATIC        ON  CACHE BOOL "" FORCE)  # want one dead-strippable binary (ADR 0023)
set(SDL_TEST_LIBRARY  OFF CACHE BOOL "" FORCE)
set(SDL_TESTS         OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES      OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL       OFF CACHE BOOL "" FORCE)
set(SDL_UNINSTALL     OFF CACHE BOOL "" FORCE)

set(SDL_VIDEO         ON  CACHE BOOL "" FORCE)
set(SDL_GPU           ON  CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN     ON  CACHE BOOL "" FORCE)

set(SDL_AUDIO         OFF CACHE BOOL "" FORCE)
set(SDL_RENDER        OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK      OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC        OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI        OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR        OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA        OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG        OFF CACHE BOOL "" FORCE)
set(SDL_TRAY          OFF CACHE BOOL "" FORCE)
set(SDL_POWER         OFF CACHE BOOL "" FORCE)

add_subdirectory(sdl3 ${CMAKE_BINARY_DIR}/third_party/sdl3 SYSTEM)

# ---------------------------------------------------------------------------
# The shader toolchain (ADR 0006: HLSL is the one authoring language;
# ADR 0032: DirectXShaderCompiler is fetched and hash-pinned, not vendored).
#
# This is a BUILD-TIME tool and nothing else. `shadercross` runs during the
# build to turn `shaders/src/*.hlsl` into SPIR-V, DXIL and MSL; neither it nor
# SPIRV-Cross is ever linked into `luaug-host`, which would put a shader
# compiler and a backend type system inside the shipped binary (R17, ADR 0010's
# same reasoning for assimp).
#
# Both must exist before SDL_shadercross is added: with SDLSHADERCROSS_VENDORED
# off it resolves SPIRV-Cross through `find_package` unless the targets are
# already present (its CMakeLists.txt:151), and it looks for DXC through the
# find-module results cmake/luaug_dxc.cmake has already seeded.
# ---------------------------------------------------------------------------
if(LUAUG_SHADER_TOOLCHAIN)
    # SPIRV-Cross is the SPIR-V -> HLSL/MSL half of the translation. The option
    # list is an architecture statement, as with SDL3 above:
    #
    #   SHARED/CLI/TESTS/INSTALL
    #            -- we consume one static library, not a project. Static also
    #               keeps the build tree free of a DLL that would have to be
    #               staged beside every tool that runs.
    #   GLSL, HLSL, MSL
    #            -- the backends SDL_shadercross actually drives
    #               (SPVC_BACKEND_HLSL and SPVC_BACKEND_MSL in
    #               src/SDL_shadercross.c; both derive from the GLSL compiler).
    #   CPP, REFLECT, UTIL
    #            -- SPIRV-Cross's own C++-source and JSON-reflection emitters.
    #               Reflection reaches us through SDL_shadercross's API instead,
    #               so these are dead weight rather than a deferred feature.
    set(SPIRV_CROSS_STATIC          ON  CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SHARED          OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_CLI             OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS    OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SKIP_INSTALL    ON  CACHE BOOL "" FORCE)

    set(SPIRV_CROSS_ENABLE_C_API    ON  CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_GLSL     ON  CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL     ON  CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_MSL      ON  CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP      OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_REFLECT  OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_UTIL     OFF CACHE BOOL "" FORCE)

    add_subdirectory(spirv_cross ${CMAKE_BINARY_DIR}/third_party/spirv_cross SYSTEM)

    # SPIRV-Cross names its targets with hyphens and its exported packages with
    # underscores; SDL_shadercross tests for the underscore form to decide
    # whether it must go find the package. The alias is what makes "already
    # built above" answer that question.
    if(NOT TARGET spirv_cross_c)
        add_library(spirv_cross_c ALIAS spirv-cross-c)
    endif()

    # SDL_shadercross calls export(TARGETS SDL3_shadercross-static ...)
    # unconditionally (its CMakeLists.txt:234), and CMake refuses to export a
    # target whose link interface names something that is in no export set.
    # SPIRV_CROSS_SKIP_INSTALL is what turned SPIRV-Cross's own export() calls
    # off, so one is made here instead: a build-tree file nothing consumes,
    # whose only job is to satisfy that rule without bringing install rules for
    # a static library we never install back with it.
    export(TARGETS spirv-cross-core spirv-cross-glsl spirv-cross-hlsl spirv-cross-msl spirv-cross-c
        FILE "${CMAKE_BINARY_DIR}/third_party/spirv-cross-targets.cmake")

    #   VENDORED  -- its vendored path expects git submodules this repo does not
    #                carry, and would pull DXC's LLVM fork in as source
    #                (ADR 0032 is the decision not to).
    #   SPIRVCROSS_SHARED, SHARED
    #            -- static everywhere, matching SPIRV-Cross and SDL above.
    #   DXC      -- the HLSL front end. With it off, src/SDL_shadercross.c:568
    #               is a stub that only sets an error: no SPIR-V, and therefore
    #               no MSL either, since MSL is derived from SPIR-V.
    #   CLI      -- ON deliberately: `shadercross` the executable IS the tool
    #               cmake/luaug_shaders.cmake invokes.
    #   CLI_STATIC
    #            -- link it against the static SDL and static library so the
    #               tool runs out of the build tree with no staging beyond DXC's
    #               own DLLs.
    #   TESTS, INSTALL, INSTALL_CPACK, INSTALL_MAN, INSTALL_RUNTIME
    #            -- upstream's own packaging story; ours is `luaug build`.
    #   INSTALL_RUNTIME in particular would re-download DXC behind our back,
    #            which is exactly the fetch cmake/luaug_dxc.cmake owns.
    set(SDLSHADERCROSS_VENDORED         OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_SHARED           OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_STATIC           ON  CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_DXC              ON  CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_CLI              ON  CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_CLI_STATIC       ON  CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_TESTS            OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_WERROR           OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_INSTALL          OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_INSTALL_CPACK    OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_INSTALL_MAN      OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_INSTALL_RUNTIME  OFF CACHE BOOL "" FORCE)

    add_subdirectory(sdl_shadercross ${CMAKE_BINARY_DIR}/third_party/sdl_shadercross SYSTEM)

    # Host tools get their own directory rather than being scattered through
    # the third_party mirror: it is where the runtime pieces DXC needs have to
    # land, and it names what these binaries are for.
    set(LUAUG_HOST_TOOLS_DIR "${CMAKE_BINARY_DIR}/host-tools")
    set_target_properties(shadercross PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY                "${LUAUG_HOST_TOOLS_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${LUAUG_HOST_TOOLS_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${LUAUG_HOST_TOOLS_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${LUAUG_HOST_TOOLS_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${LUAUG_HOST_TOOLS_DIR}")

    # dxcompiler.dll is linked through an import library, and dxil.dll is not
    # linked at all -- dxcompiler loads it dynamically to sign the DXIL it
    # emits, and D3D12 rejects unsigned DXIL outside Developer Mode. Both have
    # to sit beside the executable, so staging them is a build dependency of the
    # tool rather than a step someone has to remember.
    if(LUAUG_DXC_RUNTIME_FILES)
        set(_luaug_dxc_staged "")
        foreach(_luaug_dxc_file IN LISTS LUAUG_DXC_RUNTIME_FILES)
            get_filename_component(_luaug_dxc_name "${_luaug_dxc_file}" NAME)
            add_custom_command(
                OUTPUT "${LUAUG_HOST_TOOLS_DIR}/${_luaug_dxc_name}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_luaug_dxc_file}" "${LUAUG_HOST_TOOLS_DIR}/${_luaug_dxc_name}"
                DEPENDS "${_luaug_dxc_file}"
                COMMENT "Staging ${_luaug_dxc_name} beside the shader compiler"
                VERBATIM)
            list(APPEND _luaug_dxc_staged "${LUAUG_HOST_TOOLS_DIR}/${_luaug_dxc_name}")
        endforeach()
        add_custom_target(luaug_dxc_runtime DEPENDS ${_luaug_dxc_staged})
        add_dependencies(shadercross luaug_dxc_runtime)
    endif()
endif()

# ---------------------------------------------------------------------------
# Dear ImGui (ADR 0011): the debug overlay now, the editor later. The in-game
# UI is a different problem with a different answer -- a retained tree of
# Roblox-shaped Instances compiled to Clay -- and ImGui is never part of it.
#
# Dev-facing only, and ADR 0011 states that as a property of the BUILD rather
# than of the code. Gating the target instead of the call sites is what makes
# "a shipping binary contains no ImGui" a checkable claim: there is no ImGui
# object file to find because none was compiled. `app`'s overlay is written so
# that it degrades to a class whose methods do nothing, not to an #ifdef at
# every call site.
#
# LUAUG_DEBUG_UI is declared in cmake/luaug_options.cmake, beside the other
# build-time toggles, and consumed here.

if(LUAUG_DEBUG_UI)
    # ImGui ships no build system of its own -- upstream's position is that you
    # compile the sources you need -- so the target is written here. The list is
    # an architecture statement, the same way SDL3's option list above is:
    #
    #   imgui, imgui_draw, imgui_tables, imgui_widgets
    #            -- the library. Not four features: one library that happens to
    #               be split across four files.
    #   imgui_demo
    #            -- absent. Its ten thousand lines exist to be read next to
    #               ShowDemoWindow(), and nothing in this engine calls it.
    #   imgui_impl_sdl3
    #            -- the platform half. It consumes raw SDL_Events, which is
    #               precisely why platform::rawEvents() exists (ADR 0004's
    #               sdl_interop.h): the engine models the handful of events it
    #               reacts to, and hands the full stream to this.
    #   imgui_impl_sdlgpu3
    #            -- the renderer half, matching the default RHI backend
    #               (ADR 0005). The twenty other backends upstream ships are for
    #               other people's engines. Its shaders are embedded in
    #               imgui_impl_sdlgpu3_shaders.h, so it asks nothing of our own
    #               shader toolchain.
    add_library(luaug_imgui STATIC
        imgui/imgui.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/backends/imgui_impl_sdl3.cpp
        imgui/backends/imgui_impl_sdlgpu3.cpp)
    add_library(luaug::imgui ALIAS luaug_imgui)

    # SYSTEM, so imgui.h is exempt from our warnings-as-errors bar in the files
    # that include it; the sources above are exempt by the same rule every other
    # vendored tree is, which is that nothing here links luaug::warnings.
    target_include_directories(luaug_imgui SYSTEM PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends")

    # PRIVATE, exactly as `platform` links it. The two backend headers take
    # SDL_Window and SDL_GPUDevice in their signatures, so a consumer needs SDL
    # on its own include path anyway -- and making it say so in its CMakeLists
    # is the same deliberate crossing sdl_interop.h charges for.
    target_link_libraries(luaug_imgui PRIVATE SDL3::SDL3-static)
endif()

# ---------------------------------------------------------------------------
# The asset pipeline (ADR 0010): glTF in, optimized meshes out. Wired here from
# M4, which is the milestone whose `asset` module links them.
#
# simdjson is first and it is not optional: fastgltf requires it
# unconditionally (its CMakeLists.txt:77-82) and, with no `simdjson::simdjson`
# target already defined, downloads the amalgamated pair from
# raw.githubusercontent.com into its own vendored tree at configure time, with
# no hash check -- R5, R13, R14 and ADR 0032's fetch rule in one upstream
# default. ADR 0036 vendors simdjson and defines that target here, BEFORE
# fastgltf is added, so the download branch is unreachable; a patch on fastgltf
# turns that branch into a FATAL_ERROR so a future version cannot quietly
# resume fetching.
#
# The amalgamated pair is compiled directly rather than by adding simdjson's
# own subdirectory: upstream's CMake builds tests, benchmarks and tools, and
# its singleheader/ directory exists to *regenerate* the amalgamation with
# Python rather than to compile it. Two files are what fastgltf consumes.
add_library(luaug_simdjson STATIC "${CMAKE_CURRENT_SOURCE_DIR}/simdjson/singleheader/simdjson.cpp")
add_library(simdjson::simdjson ALIAS luaug_simdjson)
# BUILD_INTERFACE, not a bare path: naming this target in fastgltf's export set
# below makes CMake check its interface for source-tree paths, and a target that
# advertised one would be describing a directory that does not exist on whatever
# machine consumed the installed package. fastgltf's own include directories are
# written the same way (its CMakeLists.txt:81).
target_include_directories(luaug_simdjson SYSTEM PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/simdjson/singleheader>)
target_compile_features(luaug_simdjson PUBLIC cxx_std_17)

#   COMPILE_AS_CPP20 -- the engine is C++20 and fastgltf's types cross the seam
#                       into our headers; compiling it as C++17 beside C++20
#                       callers is an ODR hazard nobody needs.
#   TESTS/EXAMPLES/DOCS/GLTF_RS/ASSIMP
#                    -- upstream's own benchmark and test rig. All default OFF;
#                       set explicitly because `cmake/dependencies.cmake`
#                       FetchContent-declares glm, Catch2 and corrosion the
#                       moment any of them is ON, and a network fetch during
#                       configure is exactly what ADR 0036 exists to prevent.
set(FASTGLTF_COMPILE_AS_CPP20 ON  CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_TESTS     OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS      OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_GLTF_RS   OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_ASSIMP    OFF CACHE BOOL "" FORCE)
add_subdirectory(fastgltf ${CMAKE_BINARY_DIR}/third_party/fastgltf SYSTEM)

# fastgltf's install rules are unconditional -- it exposes no INSTALL option --
# and `install(EXPORT fastgltf-targets)` refuses to generate while anything in
# fastgltf's link interface sits outside an export set. `luaug_simdjson` is
# ours and is linked PRIVATE, which a static library still records as
# `$<LINK_ONLY:...>` in its interface, so the check finds it and stops the
# whole configure.
#
# Naming it in the same export set satisfies that check. Nothing is ever
# installed from this build -- `luaug build` (M8) is the packaging story -- so
# this is a generate-time formality rather than an install decision, and it is
# the same shape as the `export(TARGETS spirv-cross-...)` line above, which
# exists for the mirror-image reason.
install(TARGETS luaug_simdjson EXPORT fastgltf-targets)

# The guard is only a guard if it is checked. If fastgltf ever populates its
# deps/ directory, the download branch ran, and this repository has quietly
# gained an unpinned dependency fetched over the network.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fastgltf/deps/simdjson")
    message(FATAL_ERROR
        "fastgltf downloaded simdjson into its vendored tree -- the ADR 0036 guard has been defeated. "
        "Delete third_party/fastgltf/deps and re-run `lute tools/repo/vendor.luau sync fastgltf`.")
endif()

#   DEMO/GLTFPACK -- upstream's sample viewer and CLI; we link the library.
#   INSTALL       -- defaults ON and would add install rules for a static
#                    library nothing installs.
set(MESHOPT_BUILD_DEMO     OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(meshoptimizer ${CMAKE_BINARY_DIR}/third_party/meshoptimizer SYSTEM)

# Restored so nothing above this directory inherits the ceiling. Our own build
# does not shell out to git during configure today, and this is what keeps that
# from becoming a silent dependency if it ever does.
set(ENV{GIT_CEILING_DIRECTORIES} "${LUAUG_SAVED_GIT_CEILING}")
