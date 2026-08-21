# M7's gate, as two steps: build the example's assets, then fly over them.
#
# Two steps rather than one because the chunk SOURCES are in the repository and
# the compiled `.lchunk` payloads are not -- `.luaug/` is ignored, deliberately,
# since a build output in git is a merge conflict waiting to be resolved by
# guessing. So a fresh clone has a world to build and no world to stream, and a
# gate that assumed otherwise would pass locally and fail in CI.
#
# Which also makes this the asset-build determinism check's other half: the pack
# is compiled here from the same sources on every tier, and `assetc` is the tool
# whose output the determinism test hashes.
cmake_minimum_required(VERSION 3.24)

foreach(required HOST ASSETC PROJECT REPORT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_soak_gate.cmake needs -D${required}=")
    endif()
endforeach()

set(built "${PROJECT}/.luaug")
file(MAKE_DIRECTORY "${built}")

execute_process(
    COMMAND "${ASSETC}"
        --input "${PROJECT}/content"
        --output "${built}/content.lpack"
        --manifest "${built}/content.manifest.json"
    RESULT_VARIABLE compileResult
    OUTPUT_VARIABLE compileOutput
    ERROR_VARIABLE compileOutput)
if(NOT compileResult EQUAL 0)
    message(FATAL_ERROR "assetc failed (${compileResult}):\n${compileOutput}")
endif()
message(STATUS "${compileOutput}")

# `--rhi=null` and this is a decision rather than a convenience. The gate asks
# whether STREAMING hitches, and it cannot attribute a hitch to a cause -- so a
# GPU driver's scheduling on whatever machine CI happened to allocate would land
# in the same histogram as a chunk that took too long to materialise, and the
# first flaky failure would teach everyone to ignore the gate. What is removed
# is the renderer; what is measured -- residency decisions, chunk decode,
# instance materialisation and eviction, physics, the tick -- is all still here.
#
# The consequence is stated so nobody has to rediscover it: a leak that is
# purely GPU-side is invisible to this test. `--soak-min-instances` is what
# stops the whole run being invisible.
execute_process(
    COMMAND "${HOST}" "${PROJECT}"
        --headless --rhi=null --frames=${FRAMES} --exit
        --soak-report=${REPORT}
        --soak-ceiling-mb=${CEILING_MB}
        --soak-min-instances=${MIN_INSTANCES}
    RESULT_VARIABLE soakResult
    OUTPUT_VARIABLE soakOutput
    ERROR_VARIABLE soakOutput)
message(STATUS "${soakOutput}")
if(NOT soakResult EQUAL 0)
    message(FATAL_ERROR "the soak gate failed (${soakResult}); the report is at ${REPORT}")
endif()
