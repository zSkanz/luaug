# L2 -- runtime asset loading (architecture.md §2 `asset`, ADR 0010).
#
# Born at M4 as a loader, not as the streaming system architecture.md describes
# in full: mount points, URN resolution, meshes, materials and images are what a
# glTF scene needs to reach the renderer. StreamingManager, chunk manifests,
# `.lpack` and content addressing arrive with M7, which is the milestone that
# has something to stream.
#
# Knows nothing about rhi: this module produces CPU-side resources and `render`
# owns every upload (architecture.md §2).

luaug_add_module(asset
    LAYER 2
    SOURCES
        src/content.cpp
        src/image.cpp
        src/gltf.cpp
        src/mesh_format.cpp
        src/pack.cpp
        src/primitives.cpp
        src/texture.cpp
    DEPS
        core
        platform
)

# stb_image and stb_image_write. Header-only and SYSTEM, so upstream's warnings
# are not our warnings-as-errors bar; both implementations are compiled exactly
# once, in src/image.cpp.
target_include_directories(luaug_asset SYSTEM PRIVATE "${LUAUG_THIRD_PARTY_DIR}/stb")

# fastgltf and meshoptimizer are PRIVATE, and that is the point of this module:
# no fastgltf type appears in `luaug/asset/*.h`, so `render` and `scene` link an
# importer without inheriting a parser's headers. R17 is about the Luau API, but
# the same instinct applies one layer down.
target_link_libraries(luaug_asset PRIVATE fastgltf::fastgltf meshoptimizer)

# The basis TRANSCODER and never the encoder (ADR 0010, third_party/CMakeLists).
# The engine reads textures; only the offline `assetc` tool writes them, and
# keeping the encoder out of this link line is what makes that a property of the
# build rather than a promise.
target_link_libraries(luaug_asset PRIVATE basis::transcoder)

luaug_add_module_tests(asset
    MODULES
        asset
    SOURCES
        tests/content_tests.cpp
        tests/image_tests.cpp
        tests/gltf_tests.cpp
        tests/mesh_format_tests.cpp
        tests/pack_tests.cpp
        tests/primitive_tests.cpp
        tests/texture_tests.cpp
)

if(LUAUG_BUILD_TESTS)
    # The glTF fixtures are read from the source tree rather than copied beside
    # the binary: `importGltf` resolves a relative URI against the directory it
    # is handed, and `textured.gltf` names a `.png` sibling -- a copy step would
    # have to keep the pair together, and getting that wrong would show up as a
    # missing texture rather than as a build failure.
    target_compile_definitions(luaug_asset_tests PRIVATE
        LUAUG_TEST_CATALOG="${CMAKE_SOURCE_DIR}/i18n/en.json"
        LUAUG_ASSET_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
