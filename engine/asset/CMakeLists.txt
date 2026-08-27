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
        src/chunk.cpp
        src/content.cpp
        src/terrain.cpp
        src/terrain_mesher.cpp
        src/terrain_raycast.cpp
        src/terrain_cell.cpp
        src/terrain_edit.cpp
        src/image.cpp
        src/gltf.cpp
        src/mesh_format.cpp
        src/model_split.cpp
        src/pack.cpp
        src/primitives.cpp
        src/streaming.cpp
        src/texture.cpp
    DEPS
        core
        platform
)

# stb_image and stb_image_write. Header-only and SYSTEM, so upstream's warnings
# are not our warnings-as-errors bar; both implementations are compiled exactly
# once, in src/image.cpp.
target_include_directories(luaug_asset SYSTEM PRIVATE "${LUAUG_THIRD_PARTY_DIR}/stb")

# xxhash, for the per-object digests a terrain tile and brick carry (ADR 0067).
# Header-only through `XXH_INLINE_ALL`, PRIVATE for the same reason `scene` takes
# it that way: nothing above needs the header, because a digest crosses the seam
# as a `u64`.
target_include_directories(luaug_asset SYSTEM PRIVATE "${LUAUG_THIRD_PARTY_DIR}/xxhash")

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
        tests/chunk_tests.cpp
        tests/content_tests.cpp
        tests/terrain_tests.cpp
        tests/terrain_mesher_tests.cpp
        tests/terrain_cell_tests.cpp
        tests/image_tests.cpp
        tests/gltf_tests.cpp
        tests/mesh_format_tests.cpp
        tests/model_split_tests.cpp
        tests/pack_tests.cpp
        tests/primitive_tests.cpp
        tests/streaming_tests.cpp
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

    # The model the skipped import bench measures. Overridable, because the
    # interesting answer comes from somebody's own asset -- the fixtures here are
    # three kilobytes and answer "roughly zero" for every question worth asking.
    if(NOT DEFINED LUAUG_BENCH_GLTF)
        set(LUAUG_BENCH_GLTF "${CMAKE_SOURCE_DIR}/examples/02-meshes/content/models/scene.gltf")
    endif()
    target_compile_definitions(luaug_asset_tests PRIVATE
        LUAUG_BENCH_GLTF="${LUAUG_BENCH_GLTF}")
endif()
