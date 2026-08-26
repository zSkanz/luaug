# tools/assetc -- the offline asset compiler (roadmap M7, M7 brief Decision 1).
#
# Unlike tools/imgcmp, this one DOES link engine modules, and that is the point:
# `asset::encodeMesh` and `asset::decodeMesh` are one implementation shared by
# the tool that writes a pack and the runtime that reads it. Two would drift,
# and the milestone that discovered they had drifted would discover it as a
# world that renders wrong.
#
# The basis ENCODER is linked here and nowhere else. `engine/asset` links only
# the transcoder, so "the engine cannot write a texture" is a property of the
# build rather than a promise.

add_library(luaug_assetc_lib STATIC
    src/compiler.cpp
    src/texture.cpp
    src/exotic.cpp
)
target_include_directories(luaug_assetc_lib PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(luaug_assetc_lib
    PUBLIC luaug::asset luaug::core
    # `jobs` for the parallel texture encode (E9 step 11). Its determinism rule
    # is what makes it usable here at all: per-job buffers, a barrier, and a
    # merge in source order, so the pack is built from the same bytes in the same
    # sequence however many workers ran.
    PRIVATE luaug::warnings luaug::platform luaug::jobs basis::encoder)

# assimp, and ONLY here (ADR 0010). The engine reads `.lmesh`, a format assimp
# has never heard of, and the layering gate reading real `#include` edges is what
# keeps that true rather than a comment.
#
# The definition is always set so `exotic.cpp` compiles either way: with the
# importer off it becomes a keyed error naming the extension, which is a better
# answer than a link failure or a model silently treated as an opaque blob.
if(LUAUG_ASSETC_ASSIMP)
    target_compile_definitions(luaug_assetc_lib PRIVATE LUAUG_ASSETC_ASSIMP=1)
    target_link_libraries(luaug_assetc_lib PRIVATE assimp::assimp)
else()
    target_compile_definitions(luaug_assetc_lib PRIVATE LUAUG_ASSETC_ASSIMP=0)
endif()

add_executable(assetc src/main.cpp)
target_link_libraries(assetc PRIVATE luaug::warnings luaug_assetc_lib luaug::platform luaug::jobs)

# The catalog beside the binary, for the same reason `luaug-host` gets one: an
# engine error this tool relays should read as prose rather than as a bare key.
add_custom_command(TARGET assetc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/i18n/en.json"
        "$<TARGET_FILE_DIR:assetc>/content/i18n/en.json"
    COMMENT "Staging the engine message catalog for assetc")

if(LUAUG_BUILD_TESTS)
    add_executable(luaug_assetc_tests
        tests/compiler_tests.cpp
        tests/texture_tests.cpp
    )
    target_link_libraries(luaug_assetc_tests PRIVATE
        luaug::warnings
        doctest::doctest_with_main
        luaug_assetc_lib
        luaug::platform
    )
    target_compile_definitions(luaug_assetc_tests PRIVATE
        LUAUG_TEST_CATALOG="${CMAKE_SOURCE_DIR}/i18n/en.json"
        LUAUG_ASSET_TEST_DATA="${CMAKE_SOURCE_DIR}/engine/asset/tests/data"
        LUAUG_ASSETC_ASSIMP=$<BOOL:${LUAUG_ASSETC_ASSIMP}>)
    add_test(NAME assetc COMMAND luaug_assetc_tests)
endif()
