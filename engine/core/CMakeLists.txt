# L0 -- depends on nothing. Deliberately does not include Luau headers:
# architecture.md §2 reserves those for `script` (and the host above it), so
# core stays usable by tooling and tests that have no VM.

luaug_add_module(core
    LAYER 0
    SOURCES
        src/i18n.cpp
        src/json.cpp
        src/json_writer.cpp
        src/log.cpp
        src/error.cpp
        src/math.cpp
        src/phase.cpp
        src/name_atom.cpp
        src/random.cpp
)

luaug_add_module_tests(core
    SOURCES
        tests/text_key_tests.cpp
        tests/i18n_tests.cpp
        tests/json_tests.cpp
        tests/json_writer_tests.cpp
        tests/error_tests.cpp
        tests/math_tests.cpp
        tests/phase_tests.cpp
        tests/slotmap_tests.cpp
        tests/name_atom_tests.cpp
        tests/random_tests.cpp
)
