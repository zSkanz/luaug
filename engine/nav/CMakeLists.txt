# L4 -- navigation (ADR 0022, integrated by ADR 0089).
#
# A tiled navmesh over Recast and Detour, built from the scene's static
# geometry where queries ask for it. `scene` for what stands in the world,
# `asset` for the terrain mesher the renderer and the collider already use, and
# Recast privately: no Recast type crosses `nav.h` (R17).
if(LUAUG_NAV_RECAST)
    luaug_add_module(nav
        LAYER 4
        SOURCES
            src/recast_navigation.cpp
        DEPS
            core
            scene
            asset
        PRIVATE_DEPS
            recastnavigation
    )

    if(LUAUG_BUILD_TESTS)
        luaug_add_module_tests(nav
            MODULES
                nav
                scene
            SOURCES
                tests/navigation_tests.cpp
        )
    endif()
else()
    # A build without it still has the header, so the script bindings compile
    # and answer "no navigation" rather than failing to link.
    add_library(luaug_nav INTERFACE)
    add_library(luaug::nav ALIAS luaug_nav)
    target_include_directories(luaug_nav INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(luaug_nav INTERFACE luaug::core)
endif()
