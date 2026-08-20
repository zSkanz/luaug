# Stage everything the triangle APK packages, into one directory.
#
#   cmake -DLIBRARY=<android-build>/samples/triangle/libmain.so \
#         -DSHADER_DIR=<host-build>/samples/triangle/content/shaders \
#         -DCATALOG=<repo>/i18n/en.json \
#         -DABI=arm64-v8a \
#         -DSTAGE=<somewhere under $LUAUG_BUILD_ROOT> \
#         -P cmake/stage_android_apk.cmake
#
# Why a staging step exists at all: LUAUG_SHADER_TOOLCHAIN defaults OFF when
# cross-compiling (cmake/luaug_options.cmake -- shadercross is a HOST tool and
# DirectXShaderCompiler publishes no Android artifact), so the Android build
# produces a library and no shaders. The SPIR-V therefore comes from a Tier-1 or
# Tier-2 *host* build of the same commit, and something has to bring the two
# halves together. This is that something.
#
# Why a CMake script rather than a shell script: the same three commands have to
# work from PowerShell on the dev machine and from bash on the runner, and CMake
# is the one interpreter both already have.
#
# Why STAGE is outside the repository: this is build output, and R14 keeps build
# output out of the source tree. `samples/triangle/android/app/build.gradle`
# reads the location from `-Pluaug.stageDir` for the same reason -- nothing is
# ever written into `app/src/main/`.

foreach(required LIBRARY SHADER_DIR CATALOG ABI STAGE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "stage_android_apk.cmake: -D${required}=... is required")
    endif()
endforeach()

# Wiped rather than merged. A stale blob from a previous commit is the failure
# this whole checkpoint cannot afford: it would put a triangle on the phone that
# does not correspond to the tree, and nothing on the device would say so.
file(REMOVE_RECURSE "${STAGE}")

set(assets "${STAGE}/assets/content")
set(jni_libs "${STAGE}/jniLibs/${ABI}")

if(NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR
        "stage_android_apk.cmake: no native library at '${LIBRARY}'.\n"
        "Cross-compile it first: cmake --build <android-build> --target luaug_triangle_sample")
endif()
file(MAKE_DIRECTORY "${jni_libs}")
# Named libmain.so whatever the source file is called: SDLActivity derives the
# shared object from getLibraries() and dlopen()s exactly that name. The Android
# branch of samples/triangle/CMakeLists.txt already sets OUTPUT_NAME to match,
# so this is a copy and not a rename -- asserted rather than assumed.
get_filename_component(library_name "${LIBRARY}" NAME)
if(NOT library_name STREQUAL "libmain.so")
    message(FATAL_ERROR
        "stage_android_apk.cmake: expected libmain.so, got '${library_name}'. "
        "SDLActivity loads the library by name; renaming it here would only move the failure to the device.")
endif()
file(COPY "${LIBRARY}" DESTINATION "${jni_libs}")

# The manifest first, because its absence has one cause worth naming precisely:
# a host build whose shader toolchain was off. Everything else downstream would
# report "no shaders for format spirv" from inside the phone, where nobody can
# see it.
if(NOT EXISTS "${SHADER_DIR}/manifest.json")
    message(FATAL_ERROR
        "stage_android_apk.cmake: no shader manifest at '${SHADER_DIR}/manifest.json'.\n"
        "Build the shaders on a HOST tier first:\n"
        "  cmake --build <host-build> --target luaug_triangle_sample_shaders\n"
        "(and check that host configured with LUAUG_SHADER_TOOLCHAIN=ON -- it is off when cross-compiling).")
endif()
file(MAKE_DIRECTORY "${assets}/shaders")
file(COPY "${SHADER_DIR}/manifest.json" DESTINATION "${assets}/shaders")

# SPIR-V only. Android's SDL_GPU backend is Vulkan (ADR 0005), so the DXIL and
# MSL blocks the same build produced would be dead weight in the package; the
# manifest keeps naming them and ShaderLibrary only ever reads the key for the
# format the device reported.
file(GLOB spirv_blobs "${SHADER_DIR}/spirv/*.spv")
if(NOT spirv_blobs)
    message(FATAL_ERROR "stage_android_apk.cmake: the manifest exists but '${SHADER_DIR}/spirv' holds no .spv blobs")
endif()
file(COPY ${spirv_blobs} DESTINATION "${assets}/shaders/spirv")

# The reflection sidecars are not optional: SDL_GPU rejects a shader whose
# declared resource counts disagree with its bindings, so ShaderLibrary::load
# fails outright when one is missing (engine/render/src/shader_library.cpp).
file(GLOB reflect_files "${SHADER_DIR}/reflect/*.json")
if(NOT reflect_files)
    message(FATAL_ERROR "stage_android_apk.cmake: '${SHADER_DIR}/reflect' holds no reflection sidecars")
endif()
file(COPY ${reflect_files} DESTINATION "${assets}/shaders/reflect")

# Copied from the source tree rather than from the host build, because the
# sample stages it with a POST_BUILD rule that only runs when the sample itself
# links -- and the host build above deliberately builds only its shaders.
if(NOT EXISTS "${CATALOG}")
    message(FATAL_ERROR "stage_android_apk.cmake: no message catalog at '${CATALOG}'")
endif()
file(MAKE_DIRECTORY "${assets}/i18n")
file(COPY "${CATALOG}" DESTINATION "${assets}/i18n")

message(STATUS "Staged the triangle APK payload in ${STAGE}")
