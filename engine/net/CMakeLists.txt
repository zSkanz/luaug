# L2 -- the network seam (architecture.md §2 `net_api`, ADR 0012).
#
# v1 ships primitives only, and M3 needs exactly one of them: the WebSocket
# CLIENT the engine dials the dev server with (ADR 0035). Nothing here is bound
# to Luau and no game script can reach it (R17); `@std/net` in the game VM is M7.
#
# The protocol half is separated from the socket half so that every rule RFC
# 6455 states is tested against the RFC's own published vectors rather than
# against our output.

luaug_add_module(net_api
    LAYER 2
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include
    SOURCES
        src/digest.cpp
        src/websocket_protocol.cpp
    DEPS
        core
)

luaug_add_module_tests(net
    MODULES
        net_api
    SOURCES
        tests/digest_tests.cpp
        tests/websocket_protocol_tests.cpp
)

if(LUAUG_BUILD_TESTS)
    target_compile_definitions(luaug_net_tests PRIVATE
        LUAUG_TEST_CATALOG="${CMAKE_SOURCE_DIR}/i18n/en.json")
endif()
