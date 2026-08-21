# L5 -- the retained tree of screen-space GUI instances (architecture.md §2
# `ui`, ADR 0011 as amended by ADR 0040).
#
# Deps are core, scene, input and render, which is what architecture.md names.
# `asset` joins them when an `ImageLabel` can name a texture the pipeline loads;
# until then this module reads no file but the one font it embeds.
#
# **No Clay.** ADR 0011 chose it as the layout solver and ADR 0040 records why v1
# does not call it: a `UDim2` placement is two multiplies and an add rather than
# a constraint, and Clay cannot express an unclamped scale or a fractional anchor
# point. It stays vendored and pinned -- removing a dependency is a human
# decision (R5) -- and nothing here includes it.

luaug_add_module(ui
    LAYER 5
    SOURCES
        src/layout.cpp
        src/text.cpp
        src/draw.cpp
        src/interaction.cpp
        src/native_accessors.cpp
        generated/class_descriptors.gen.cpp
    DEPS
        core
        input
        render
        scene
)

# stb_truetype, header-only and SYSTEM so that upstream's warnings are not our
# warnings-as-errors bar. Its implementation is compiled exactly once, in
# src/text.cpp -- the same arrangement `asset` uses for stb_image.
target_include_directories(luaug_ui SYSTEM PRIVATE "${LUAUG_THIRD_PARTY_DIR}/stb")

luaug_add_module_tests(ui
    MODULES
        ui
    SOURCES
        tests/layout_tests.cpp
        tests/interaction_tests.cpp
)
