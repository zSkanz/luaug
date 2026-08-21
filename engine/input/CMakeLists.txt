# L4 -- the Input Action System (architecture.md §2 `input`, ADR 0029).
#
# Deps are core, platform and scene, which is exactly what architecture.md names
# and exactly what the module does: read a frame of `platform::Event`, resolve it
# against an Instance tree `scene` owns, and write the result back into that
# tree. It links no backend and no renderer -- a UI hit test reaches this module
# from above (`ui` is L5), never the other way round.

luaug_add_module(input
    LAYER 4
    SOURCES
        src/input.cpp
        src/native_accessors.cpp
        generated/class_descriptors.gen.cpp
    DEPS
        core
        platform
        scene
)

# The generated reflection tables for the classes this module owns. Checked in
# and freshness-gated by scripts/gates/luau-check.sh, like scene's and render's.
#
# No include directory for them, deliberately, and for the reason `render`'s
# CMakeLists gives at length: every module's generated header is named
# `class_descriptors.gen.h`, so a second one on any include path would turn
# `#include "class_descriptors.gen.h"` into a search-order lottery in `app`,
# which links all three. The one file that needs this reaches it by a relative
# path instead.

luaug_add_module_tests(input
    MODULES
        input
    SOURCES
        tests/input_tests.cpp
)
