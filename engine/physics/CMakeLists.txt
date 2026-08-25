# L2 -- the physics seam and its backend (ADR 0007, ADR 0023).
#
# One directory, two targets, exactly as `rhi/` is arranged. `physics_api` is
# header-only and is all any module above L2 ever sees; the Jolt backend has no
# public header at all -- its whole implementation is one file under src/,
# reachable only through the creator in luaug/physics/backends.h. That, and not
# the layer table, is what makes "no JPH type can reach the Luau bindings" (R17)
# a structural fact rather than a rule somebody has to remember.

luaug_add_module(physics_api
    INTERFACE
    LAYER 2
    DEPS
        core
)

# Declared on the seam rather than on the backend, because backends.h -- which
# lives in the seam -- is what reads it. A backend that is off is not declared,
# so selecting it fails to compile instead of returning null at startup.
target_compile_definitions(luaug_physics_api INTERFACE
    LUAUG_PHYSICS_JOLT=$<BOOL:${LUAUG_PHYSICS_JOLT}>
)

set(luaug_physics_test_modules physics_api)

if(LUAUG_PHYSICS_JOLT)
    luaug_add_module(physics_jolt
        LAYER 2
        SOURCES
            src/jolt/jolt_physics.cpp
        DEPS
            core
        API_DEPS
            physics_api
        PRIVATE_DEPS
            Jolt
    )

    # Jolt is compiled with its own flags and warning bar (third_party/
    # CMakeLists.txt turns its /Wall /WX off), but OUR translation unit includes
    # its headers and IS held to `luaug::warnings`. Jolt's headers are added
    # SYSTEM by the vendored target, which is what keeps that honest: our code
    # is warned about, upstream's is not.
    list(APPEND luaug_physics_test_modules physics_jolt)
endif()

luaug_add_module_tests(physics
    SOURCES
        tests/constraint_tests.cpp
        tests/physics_tests.cpp
    MODULES
        ${luaug_physics_test_modules}
)
