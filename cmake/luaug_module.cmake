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
    cmake_parse_arguments(ARG "" "LAYER" "SOURCES;DEPS;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})

    if(NOT DEFINED ARG_LAYER)
        message(FATAL_ERROR "luaug_add_module(${name}): LAYER is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "luaug_add_module(${name}): SOURCES is required")
    endif()

    set(target "luaug_${name}")
    add_library(${target} STATIC ${ARG_SOURCES})
    add_library(luaug::${name} ALIAS ${target})

    set_property(GLOBAL APPEND PROPERTY LUAUG_MODULES ${name})
    set_property(GLOBAL PROPERTY LUAUG_MODULE_LAYER_${name} ${ARG_LAYER})

    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

    # Every module we author is held to the full warning bar; vendored code is
    # exempt by being added SYSTEM.
    target_link_libraries(${target} PRIVATE luaug::warnings)

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
                "A module may only depend on strictly lower layers (architecture.md §2).")
        endif()
        target_link_libraries(${target} PUBLIC luaug::${dep})
    endforeach()

    if(ARG_PUBLIC_DEPS)
        target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_DEPS})
    endif()
    if(ARG_PRIVATE_DEPS)
        target_link_libraries(${target} PRIVATE ${ARG_PRIVATE_DEPS})
    endif()
endfunction()

# One test executable per module (architecture.md §9), registered with ctest.
function(luaug_add_module_tests name)
    if(NOT LUAUG_BUILD_TESTS)
        return()
    endif()

    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "luaug_add_module_tests(${name}): SOURCES is required")
    endif()

    set(target "luaug_${name}_tests")
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE
        luaug::${name}
        luaug::warnings
        doctest::doctest_with_main
        ${ARG_DEPS})

    add_test(NAME ${name} COMMAND ${target})
endfunction()
