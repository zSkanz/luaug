# Module declaration helper with mechanical layer enforcement
# (architecture.md §2).
#
# The layering is the engine's main defence against turning into a ball of mud,
# so it is enforced at declaration time here (a module may only name deps at
# strictly lower layers) and again in CI by tools/repo/checklayers.luau, which
# reads the actual #include lines. Both are needed: this catches a wrong
# link-time dependency, the include checker catches a header sneaking across a
# seam without a link edge.
#
#   L0 core
#   L1 jobs, platform
#   L2 rhi_api, physics_api, net_api, audio, asset
#   L3 scene
#   L4 render, input, nav
#   L5 ui, script
#   L6 app

define_property(GLOBAL PROPERTY LUAUG_MODULES
    BRIEF_DOCS "All declared LuauG modules" FULL_DOCS "All declared LuauG modules")

function(luaug_add_module name)
    cmake_parse_arguments(ARG "INTERFACE" "LAYER;INCLUDE_DIR" "SOURCES;DEPS;API_DEPS;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})

    if(NOT DEFINED ARG_LAYER)
        message(FATAL_ERROR "luaug_add_module(${name}): LAYER is required")
    endif()
    if(ARG_INTERFACE AND ARG_SOURCES)
        message(FATAL_ERROR "luaug_add_module(${name}): an INTERFACE module has no SOURCES")
    endif()
    if(NOT ARG_INTERFACE AND NOT ARG_SOURCES)
        message(FATAL_ERROR "luaug_add_module(${name}): SOURCES is required")
    endif()

    # Several targets can share one directory: the *_api seam and its backends
    # live together (architecture.md's tree has one `rhi/` folder, not four), so
    # the include root is a parameter rather than always ./include.
    if(NOT DEFINED ARG_INCLUDE_DIR)
        set(ARG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()

    set(target "luaug_${name}")

    if(ARG_INTERFACE)
        add_library(${target} INTERFACE)
        set(scope INTERFACE)
    else()
        add_library(${target} STATIC ${ARG_SOURCES})
        set(scope PUBLIC)
    endif()
    add_library(luaug::${name} ALIAS ${target})

    set_property(GLOBAL APPEND PROPERTY LUAUG_MODULES ${name})
    set_property(GLOBAL PROPERTY LUAUG_MODULE_LAYER_${name} ${ARG_LAYER})
    set_property(GLOBAL PROPERTY LUAUG_MODULE_INTERFACE_${name} ${ARG_INTERFACE})

    target_include_directories(${target} ${scope} $<BUILD_INTERFACE:${ARG_INCLUDE_DIR}>)

    # Every module we author is held to the full warning bar; vendored code is
    # exempt by being added SYSTEM. An INTERFACE target compiles nothing, so it
    # has nothing to hold to it.
    if(NOT ARG_INTERFACE)
        target_link_libraries(${target} PRIVATE luaug::warnings)
    endif()

    foreach(dep IN LISTS ARG_DEPS)
        get_property(dep_layer GLOBAL PROPERTY LUAUG_MODULE_LAYER_${dep})
        if(dep_layer STREQUAL "")
            message(FATAL_ERROR
                "luaug_add_module(${name}): depends on '${dep}', which is not declared yet.\n"
                "Modules must be added in layer order in the root CMakeLists.")
        endif()
        if(NOT dep_layer LESS ARG_LAYER)
            message(FATAL_ERROR
                "Layer violation: ${name} (L${ARG_LAYER}) may not depend on ${dep} (L${dep_layer}).\n"
                "A module may only depend on strictly lower layers (architecture.md §2).\n"
                "If ${dep} is this module's own interface seam, use API_DEPS.")
        endif()
        target_link_libraries(${target} ${scope} luaug::${dep})
    endforeach()

    # The one sanctioned same-layer edge: a backend implements the seam it sits
    # behind, and architecture.md §2 puts both at the same layer on purpose --
    # everything above sees only the seam. Kept separate from DEPS so that
    # "same layer" stays an error everywhere else, and narrowed to interface
    # targets so it cannot become a general escape hatch.
    foreach(dep IN LISTS ARG_API_DEPS)
        get_property(dep_layer GLOBAL PROPERTY LUAUG_MODULE_LAYER_${dep})
        get_property(dep_interface GLOBAL PROPERTY LUAUG_MODULE_INTERFACE_${dep})
        if(dep_layer STREQUAL "")
            message(FATAL_ERROR "luaug_add_module(${name}): API_DEPS names '${dep}', which is not declared yet.")
        endif()
        if(NOT dep_layer EQUAL ARG_LAYER)
            message(FATAL_ERROR
                "luaug_add_module(${name}): API_DEPS is for a seam at the SAME layer; "
                "${dep} is L${dep_layer} and ${name} is L${ARG_LAYER}. Use DEPS.")
        endif()
        if(NOT dep_interface)
            message(FATAL_ERROR
                "luaug_add_module(${name}): API_DEPS requires ${dep} to be an INTERFACE module.")
        endif()
        target_link_libraries(${target} ${scope} luaug::${dep})
    endforeach()

    if(ARG_PUBLIC_DEPS)
        target_link_libraries(${target} ${scope} ${ARG_PUBLIC_DEPS})
    endif()
    if(ARG_PRIVATE_DEPS)
        if(ARG_INTERFACE)
            message(FATAL_ERROR "luaug_add_module(${name}): an INTERFACE module has no PRIVATE_DEPS")
        endif()
        target_link_libraries(${target} PRIVATE ${ARG_PRIVATE_DEPS})
    endif()
endfunction()

# One test executable per module directory (architecture.md §9), registered with
# ctest under `name`. MODULES lists the LuauG targets under test when the
# directory holds more than one -- a seam and its backends, say -- and defaults
# to `name` itself.
function(luaug_add_module_tests name)
    if(NOT LUAUG_BUILD_TESTS)
        return()
    endif()

    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;MODULES" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "luaug_add_module_tests(${name}): SOURCES is required")
    endif()
    if(NOT ARG_MODULES)
        set(ARG_MODULES ${name})
    endif()

    set(target "luaug_${name}_tests")
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE
        luaug::warnings
        doctest::doctest_with_main
        ${ARG_DEPS})

    # Test-only helpers shared across modules. Headers only, and deliberately
    # not a module: nothing in `engine/` may depend on it, and it must not be
    # able to acquire a link-time half.
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)

    foreach(module IN LISTS ARG_MODULES)
        target_link_libraries(${target} PRIVATE luaug::${module})
    endforeach()

    add_test(NAME ${name} COMMAND ${target})
endfunction()
