# Generates an example's world if it is not there (D046).
#
# **Two gates depended on an artifact that nothing produced.** The streamed
# examples' chunk SOURCES are generated and gitignored, deliberately -- 4.8 MB
# of JSON from a hundred committed lines, and the roadmap asks for no giant
# binary assets in the repository. `run.bat` regenerates them for a human. What
# nothing did was regenerate them for a GATE, so `streaming_soak` and
# `asset_determinism` passed on this machine because the world happened to be
# there from a hand-run, and on a fresh clone they would have compiled an empty
# directory. Neither had ever run in CI to say so.
#
# Included by the gate drivers rather than duplicated in them, and it is the
# same step `run.bat` performs, for the same reason and in the same order.
#
# Expects: GENERATOR (the .luau file), REPO (the repository root -- the
# generator writes to a path relative to it), and WORLD (the directory whose
# absence means "generate").

if(NOT DEFINED GENERATOR OR NOT DEFINED REPO OR NOT DEFINED WORLD)
    message(FATAL_ERROR "ensure_generated_world.cmake needs -DGENERATOR=, -DREPO= and -DWORLD=")
endif()

if(EXISTS "${WORLD}")
    return()
endif()

find_program(LUAUG_LUTE_EXECUTABLE lute)
if(NOT LUAUG_LUTE_EXECUTABLE)
    message(FATAL_ERROR
        "${WORLD} does not exist and `lute` is not on PATH to generate it.\n"
        "Run scripts/bootstrap.ps1 (or ./scripts/bootstrap.sh) once, or generate it by hand:\n"
        "  lute ${GENERATOR}")
endif()

execute_process(
    COMMAND "${LUAUG_LUTE_EXECUTABLE}" "${GENERATOR}"
    WORKING_DIRECTORY "${REPO}"
    RESULT_VARIABLE generateResult
    OUTPUT_VARIABLE generateOutput
    ERROR_VARIABLE generateOutput)
message(STATUS "${generateOutput}")
if(NOT generateResult EQUAL 0)
    message(FATAL_ERROR "generating the world failed (${generateResult}):\n${generateOutput}")
endif()

if(NOT EXISTS "${WORLD}")
    message(FATAL_ERROR "the generator reported success and wrote nothing to ${WORLD}")
endif()
