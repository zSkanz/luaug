# M7's gate: "asset build determinism check in CI".
#
# **Two PROCESSES, not two calls**, and that is the whole reason this exists
# beside the in-process double-build in `luaug_assetc_tests`. An in-process pair
# shares an address space, a heap layout, a locale and an environment, so it
# cannot see the failures that actually make a content build non-reproducible:
# a hash seeded from an address, a container iterated in allocation order, a
# timestamp, a path that leaked into an output, an environment variable read
# once. Two runs of the same binary can.
#
# Every output is compared, not just the pack. A manifest that matched while the
# pack differed would be a cache that hands back the wrong bytes under the right
# name -- which is the failure mode the whole content-addressing design exists to
# make impossible, and therefore the one worth checking.
cmake_minimum_required(VERSION 3.24)

foreach(required ASSETC CONTENT WORKDIR GENERATOR REPO WORLD)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_determinism_gate.cmake needs -D${required}=")
    endif()
endforeach()

# The world before either build. Both compilations read the same sources, so a
# missing world would make this compare two empty packs and pass (D046).
include("${CMAKE_CURRENT_LIST_DIR}/../support/ensure_generated_world.cmake")

function(compile_into label)
    # An optional second argument: the `--jobs` value this run uses. **That is
    # what turns a claim into a check** (E9 step 11): textures are encoded one
    # per worker now, and the argument that this changes no byte -- per-job
    # buffers, a barrier, a merge in source order -- is prose until a serial
    # build and a parallel one are compared. `--jobs 1` is the serial one.
    set(extra "")
    if(ARGC GREATER 1)
        set(extra --jobs "${ARGV1}")
    endif()

    set(out "${WORKDIR}/${label}")
    # Removed rather than overwritten: a stale file from a previous run that
    # neither build produced would compare equal to itself and hide a real
    # difference.
    file(REMOVE_RECURSE "${out}")
    file(MAKE_DIRECTORY "${out}")
    execute_process(
        COMMAND "${ASSETC}"
            --input "${CONTENT}"
            --output "${out}/content.lpack"
            --manifest "${out}/content.manifest.json"
            ${extra}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "assetc failed for ${label} (${result}):\n${output}")
    endif()
endfunction()

# Three, not two. The first pair is what this gate has always compared: two
# processes with the same arguments, which is what catches a hash seeded from an
# address or a container iterated in allocation order. The third is SERIAL, and
# comparing it against the others is what holds the parallel texture encode to
# its claim -- a build whose bytes depended on how many workers ran would pass
# the first comparison and fail this one.
compile_into(first)
compile_into(second)
compile_into(serial 1)

# Every file either run produced, by relative path. Taken from BOTH sides and
# merged, so a file one build wrote and the other did not is a difference rather
# than something the loop never looks at.
file(GLOB_RECURSE firstFiles RELATIVE "${WORKDIR}/first" "${WORKDIR}/first/*")
file(GLOB_RECURSE secondFiles RELATIVE "${WORKDIR}/second" "${WORKDIR}/second/*")
file(GLOB_RECURSE serialFiles RELATIVE "${WORKDIR}/serial" "${WORKDIR}/serial/*")
set(allFiles ${firstFiles} ${secondFiles} ${serialFiles})
list(REMOVE_DUPLICATES allFiles)
list(SORT allFiles)

# The vacuous-pass guard, and it has to be a NUMBER rather than "not empty":
# `assetc` writes a pack and a manifest even for an empty input, so two files is
# what "it compiled nothing at all" looks like. The caller declares what its own
# content tree should produce, for the same reason the soak gate is told how many
# instances mean "the world loaded".
list(LENGTH allFiles compared)
if(DEFINED MIN_FILES AND compared LESS MIN_FILES)
    message(FATAL_ERROR
        "the determinism gate compared only ${compared} file(s), below the ${MIN_FILES} declared; "
        "${CONTENT} did not produce the content this gate is for")
endif()

set(differences "")
foreach(relative IN LISTS allFiles)
    set(a "${WORKDIR}/first/${relative}")
    set(b "${WORKDIR}/second/${relative}")
    set(c "${WORKDIR}/serial/${relative}")
    if(NOT EXISTS "${a}" OR NOT EXISTS "${b}" OR NOT EXISTS "${c}")
        list(APPEND differences "${relative}: produced by only some of the three builds")
        continue()
    endif()
    # Hashes rather than a byte compare, so a difference names a file instead of
    # dumping a megabyte of binary at whoever is reading the failure.
    file(SHA256 "${a}" hashA)
    file(SHA256 "${b}" hashB)
    file(SHA256 "${c}" hashC)
    if(NOT hashA STREQUAL hashB)
        list(APPEND differences "${relative}: two runs with the same arguments differ -- ${hashA} != ${hashB}")
    elseif(NOT hashA STREQUAL hashC)
        # Named apart from the pair above, because the two failures mean
        # different things and send somebody to different code: this one is the
        # parallel texture encode not producing what the serial path does.
        list(APPEND differences "${relative}: parallel and --jobs 1 differ -- ${hashA} != ${hashC}")
    endif()
endforeach()

if(NOT differences STREQUAL "")
    string(REPLACE ";" "\n  " report "${differences}")
    message(FATAL_ERROR
        "the asset build is not deterministic; ${compared} file(s) compared:\n  ${report}")
endif()

message(STATUS
    "asset build determinism: ${compared} file(s) byte-identical across three processes, "
    "one of them serial")
