# L3 -- the ECS and the Instance facade (architecture.md §2, §4).
#
# `core` is the only dependency, and that is the whole design rather than a
# stage it is passing through. architecture.md §2 allows scene `jobs` and
# `physics_api` as well; neither has a consumer yet, and a dependency with no
# consumer is speculative.
#
# What it must NOT have is Luau. Signal arguments are arbitrary script values
# and connections hold script functions, so the queue this module produces
# carries POD facts and nothing else -- `script` (L5) is where a fact becomes a
# handler call. The layer checker enforces the direction; the seam is what makes
# the ECS snapshottable and the world runnable headless.

luaug_add_module(scene
    LAYER 3
    SOURCES
        src/value.cpp
        src/class_registry.cpp
        src/world.cpp
        src/world_hash.cpp
        src/native_accessors.cpp
    DEPS
        core
)

# xxHash, header-only via XXH_INLINE_ALL, for WorldHash (architecture.md §9).
# SYSTEM so its warnings are not ours, PRIVATE because it appears in no header
# of ours -- nothing outside `world_hash.cpp` knows the world hash is xxh3.
target_include_directories(luaug_scene SYSTEM PRIVATE "${LUAUG_THIRD_PARTY_DIR}/xxhash")

luaug_add_module_tests(scene
    SOURCES
        tests/scene_fixture.cpp
        tests/component_pool_tests.cpp
        tests/class_registry_tests.cpp
        tests/world_tests.cpp
)
