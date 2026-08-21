# L4 -- the navigation seam (ADR 0022, roadmap M7: "vendored, seam defined, no
# integration").
#
# An INTERFACE library and nothing else. There is no `.cpp`, no Recast, and no
# link edge to `third_party/recastnavigation` anywhere in this tree -- that is
# the scope bullet, and the layering gate reading real `#include` edges is what
# keeps it true.
#
# **Why a target at all, then.** A header nobody compiles is a header that rots:
# a typo in it survives every build until the day somebody first includes it,
# which by construction is years away. The test below includes it and implements
# it with a stub, so the seam is at least known to BE valid C++ and to have an
# interface that can actually be implemented.
add_library(luaug_nav INTERFACE)
add_library(luaug::nav ALIAS luaug_nav)
target_include_directories(luaug_nav INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(luaug_nav INTERFACE luaug::core)

if(LUAUG_BUILD_TESTS)
    luaug_add_module_tests(nav
        MODULES
            core
        SOURCES
            tests/nav_seam_tests.cpp
    )
    target_link_libraries(luaug_nav_tests PRIVATE luaug::nav)
endif()
