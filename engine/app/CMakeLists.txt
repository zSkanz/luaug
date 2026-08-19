# L6 -- the host. This is the one module allowed to see everything, and for M0
# it also owns the Luau VM; M2 moves that into `engine/script` (see
# include/luaug/app/script_host.h).

luaug_add_module(app
    LAYER 6
    SOURCES
        src/script_host.cpp
    DEPS
        core
        platform
    PUBLIC_DEPS
        luaug::luau
        luaug::build_info
)

add_executable(luaug_host src/main.cpp)
set_target_properties(luaug_host PROPERTIES OUTPUT_NAME luaug-host)
target_link_libraries(luaug_host PRIVATE luaug::app luaug::warnings)

# The catalog ships beside the binary as content, because the host resolves it
# relative to its own location rather than the source tree -- the same shape a
# packaged build uses (architecture.md §8).
add_custom_command(TARGET luaug_host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/i18n/en.json"
        "$<TARGET_FILE_DIR:luaug_host>/content/i18n/en.json"
    COMMENT "Staging the engine message catalog")

luaug_add_module_tests(app
    SOURCES
        tests/script_host_tests.cpp
)

if(LUAUG_BUILD_TESTS)
    # Point the tests at the real catalog rather than a fixture, so they also
    # prove every key the host raises exists in i18n/en.json.
    target_compile_definitions(luaug_app_tests PRIVATE
        LUAUG_TEST_CATALOG="${CMAKE_SOURCE_DIR}/i18n/en.json")

    # Gate: --version must report the pin and the ABI constants (ADR 0031).
    # Asserted on the real binary rather than in-process, so it also proves the
    # provenance header actually reached the shipped executable.
    add_test(NAME host_version COMMAND luaug_host --version)
    set_tests_properties(host_version PROPERTIES
        PASS_REGULAR_EXPRESSION "Luau ${LUAUG_LUAU_VERSION} \\(${LUAUG_LUAU_COMMIT}\\)")

    add_test(NAME host_version_abi COMMAND luaug_host --version)
    set_tests_properties(host_version_abi PROPERTIES
        PASS_REGULAR_EXPRESSION "vector 3-wide f32")

    # Deliverable: `luaug-host boot.luau` prints a catalog-resolved greeting
    # and exits 0.
    add_test(NAME host_boot COMMAND luaug_host "${CMAKE_SOURCE_DIR}/examples/boot/boot.luau")
    set_tests_properties(host_boot PROPERTIES
        PASS_REGULAR_EXPRESSION "engine initialized")

    add_test(NAME host_usage_without_script COMMAND luaug_host)
    set_tests_properties(host_usage_without_script PROPERTIES WILL_FAIL TRUE)
endif()
