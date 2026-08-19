# Vendored dependency wiring (ADR 0021).
#
# Every dependency is added SYSTEM so its headers are exempt from our
# warnings-as-errors bar (architecture.md §8), and with its own tests, tools,
# examples and install rules disabled -- we consume libraries, not projects.
#
# Rows exist in manifest.json for dependencies later milestones need; they are
# wired here only once a module actually links them. Adding an add_subdirectory
# for something nothing uses would slow every build for no benefit.

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
