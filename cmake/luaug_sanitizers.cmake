# Sanitizer wiring for the `linux-clang-asan` (and future tsan/msan) presets.
# Sanitizers must reach both compile and link, and must NOT be applied to
# vendored code selectively -- a partially instrumented binary reports
# false positives at the boundary, so this is deliberately global.
#
# That argument is about AddressSanitizer and it holds. It does not hold for
# every UndefinedBehaviorSanitizer check, and `sanitize-ignorelist.txt` beside
# this file turns exactly one of them off for exactly one directory, with the
# diagnostic that forced it pasted in. Nothing else is excluded from anything.

if(NOT LUAUG_SANITIZE STREQUAL "")
    if(MSVC)
        # MSVC only ships ASan, and spells it differently.
        if(NOT LUAUG_SANITIZE STREQUAL "address")
            message(FATAL_ERROR "MSVC supports only LUAUG_SANITIZE=address (got '${LUAUG_SANITIZE}')")
        endif()
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=${LUAUG_SANITIZE} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${LUAUG_SANITIZE})

        # One exception to "global", and the file it points at says why in full:
        # a single UndefinedBehaviorSanitizer CHECK is turned off for vendored
        # code, because SPIRV-Cross's C API downcasts through opaque handles and
        # the shader compiler therefore aborted at build time before anything
        # could be linked. Everything else -- all of AddressSanitizer, every
        # other UBSan check, and all of `engine/` -- stays instrumented.
        #
        # Passed to the compiler only: an ignorelist is a compile-time decision
        # about what to instrument, and the linker has no use for it.
        add_compile_options(-fsanitize-ignorelist=${CMAKE_CURRENT_LIST_DIR}/sanitize-ignorelist.txt)
    endif()
    message(STATUS "LuauG: sanitizers enabled -> ${LUAUG_SANITIZE}")
endif()
