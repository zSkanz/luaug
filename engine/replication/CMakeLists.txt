# L4 -- the replication seam and its implementation (ADR 0069, ADR 0070).
#
# One directory, two targets, exactly as `physics/` and `rhi/` are arranged.
# `replication_api` is header-only and is all `app` ever sees; the implementation
# is reachable only through `createReplication`, which is the SINGLE caller of
# anything that binds a socket. That, and not a comment, is what makes ADR 0070's
# "no service, no property and no method can cause a bind" checkable: there is
# one door and it is reached from argument parsing.
#
# **L4 rather than L2**, and the reason is `scene`. Replication reads a world's
# components and writes them back, which means it depends on L3 -- so it sits
# above it, beside `render` and `audio`, which are here for the same reason. The
# transport it uses is L2 and is an ordinary downward edge.

luaug_add_module(replication_api
    INTERFACE
    LAYER 4
    DEPS
        core
)

# **Off by default, and that is ADR 0070 clause 3.** With it off the factory is
# not compiled, `NetworkService` reports solo, and there is no socket code in the
# binary to reach -- so a shipped single-player game cannot bind even by
# accident. It is declared on the seam rather than on the implementation because
# `replication.h` is what reads it, which is the arrangement `physics_api` uses
# for its backend flag and for the same reason: a module that is off is not
# declared, so selecting it fails to compile instead of returning null at run
# time.
option(LUAUG_ENABLE_REPLICATION "Build the replication module (post-v1 phase 4)" OFF)

target_compile_definitions(luaug_replication_api INTERFACE
    LUAUG_ENABLE_REPLICATION=$<BOOL:${LUAUG_ENABLE_REPLICATION}>
)

set(luaug_replication_test_modules replication_api)

if(LUAUG_ENABLE_REPLICATION)
    luaug_add_module(replication
        LAYER 4
        SOURCES
            src/field.cpp
            src/extract.cpp
        DEPS
            core
            scene
            # `net_api` joins when the transport does. Today this module reads a
            # world and produces bytes, and a dependency on a seam nothing calls
            # would be a link edge the layer gate has to justify for nothing.
        API_DEPS
            replication_api
    )

    # The generated schema is included as `"wire_schema.gen.h"` from src/, which
    # is the same arrangement `engine/scene` uses for its descriptor tables: a
    # generated header is an implementation detail of the module that generates
    # against it, and nothing above may include one.
    target_include_directories(luaug_replication PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

    list(APPEND luaug_replication_test_modules replication)
endif()

# **The scene fixture is compiled in rather than reached for.** It is the
# hand-built class registry every scene-facing test drives, it lives beside the
# tests that own it, and there is no library to link -- so the source joins this
# executable exactly as it joins `luaug_scene_tests`.
# **Only when the module is built.** Both test files drive the implementation
# rather than the seam, so with `LUAUG_ENABLE_REPLICATION` off there is nothing
# for them to link against -- and a test executable that cannot link is a build
# failure on every tier for a module nobody asked for.
if(LUAUG_ENABLE_REPLICATION)
luaug_add_module_tests(replication
    SOURCES
        tests/field_tests.cpp
        tests/extract_tests.cpp
        ../scene/tests/scene_fixture.cpp
    MODULES
        ${luaug_replication_test_modules}
        scene
)

if(TARGET luaug_replication_tests)
    target_include_directories(luaug_replication_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")
endif()
endif()
