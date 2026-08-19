# L4 -- architecture.md §2 eventually gives render core, jobs, platform,
# rhi_api, asset and scene. At M1 it has one file of CPU-side geometry
# generation and needs only core; a dependency with no consumer is speculative,
# and the layer helper will accept the rest when the code that uses them lands.

luaug_add_module(render
    LAYER 4
    SOURCES
        src/debug_draw.cpp
    DEPS
        core
)

luaug_add_module_tests(render
    SOURCES
        tests/debug_draw_tests.cpp
)
