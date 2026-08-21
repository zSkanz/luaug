# L3 -- the ECS and the Instance facade (architecture.md §2, §4).
#
# `core` and `physics_api`. The physics mirror lives here -- architecture.md's
# own words are "physics sync via an injected `IPhysics3D*`" -- and what it
# links is the seam headers only: no backend, no Jolt, and nothing that could
# reach one. `jobs` is still allowed and still has no consumer, and a
# dependency with no consumer is speculative.
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
        src/enum_registry.cpp
        src/world.cpp
        src/world_hash.cpp
        src/native_accessors.cpp
        src/physics_sync.cpp
        src/streaming_glue.cpp
        generated/class_descriptors.gen.cpp
    DEPS
        core
        physics_api
        # M7: the streaming glue turns an `asset::Chunk` into instances, which
        # architecture.md §2 assigns to this module ("streaming glue") and §10
        # spells out ("materialization through the same reflection path as
        # scripts"). L3 over L2 is a legal edge; what it buys is that the chunk
        # TYPE stays in the module that reads chunks off disk, rather than being
        # duplicated here so the two can drift.
        asset
)

# The generated reflection tables (architecture.md §4). Checked in and
# freshness-gated by scripts/gates/luau-check.sh rather than regenerated during
# the build, which is what keeps Lute off the critical path of a C++ compile.
#
# PUBLIC because the boot path above this layer calls
# `generated::registerClasses`. The generated header sits beside its source
# rather than under include/luaug/scene/, so that no directory holds both
# hand-written and generated files -- "do not edit" is easier to obey when it
# applies to a whole folder.
target_include_directories(luaug_scene PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/generated>)

# xxHash, header-only via XXH_INLINE_ALL, for WorldHash (architecture.md §9).
# SYSTEM so its warnings are not ours, PRIVATE because it appears in no header
# of ours -- nothing outside `world_hash.cpp` knows the world hash is xxh3.
target_include_directories(luaug_scene SYSTEM PRIVATE "${LUAUG_THIRD_PARTY_DIR}/xxhash")

luaug_add_module_tests(scene
    SOURCES
        tests/scene_fixture.cpp
        tests/component_pool_tests.cpp
        tests/class_registry_tests.cpp
        tests/enum_registry_tests.cpp
        tests/world_tests.cpp
        tests/physics_sync_tests.cpp
        tests/streaming_glue_tests.cpp
)
