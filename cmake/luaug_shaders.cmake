# HLSL -> SPIR-V / DXIL / MSL at build time (ADR 0006, architecture.md §8).
#
#     luaug_add_shaders(<target> GLOB <pattern>...)
#
# Attaches the shader build to an existing target: every matched `.hlsl` file is
# compiled once per backend format by the SDL_shadercross host tool, and the
# results land in `content/shaders/` **beside the target's executable**, next to
# a manifest the runtime reads to pick the blob for its active backend.
#
# Beside the executable, not under the binary dir, because that is where the
# engine looks: `platform::paths()` derives its content directory from the
# running binary's location, which is the shape a packaged build has. The
# message catalog is staged the same way for the same reason. Emitting straight
# there rather than emitting elsewhere and copying keeps one location and one
# write.
#
# Authoring convention, deliberately narrow while there is one pass to serve:
# one `.hlsl` file is one graphics pipeline, with entry points `VertexMain` and
# `FragmentMain`. Compute arrives with the milestone that needs it rather than
# as an unexercised branch here.
#
# Shared code goes in `shaders/include/*.hlsli`. shadercross emits no depfile,
# so every `.hlsli` is a dependency of every shader -- coarse, but a missed
# rebuild after a header edit is a far worse failure than a few extra
# recompiles of a handful of files.

function(luaug_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "GLOB")

    set(stages "vertex" "fragment")
    set(entry_vertex "VertexMain")
    set(entry_fragment "FragmentMain")

    # Backend format -> file extension and shadercross destination. The manifest
    # keys are these names, so the runtime maps its active backend to a key
    # rather than reconstructing paths.
    set(formats "spirv" "dxil" "msl")
    set(ext_spirv "spv")
    set(ext_dxil "dxil")
    set(ext_msl "msl")
    set(dest_spirv "SPIRV")
    set(dest_dxil "DXIL")
    set(dest_msl "MSL")

    if(NOT TARGET ${target})
        message(FATAL_ERROR "luaug_add_shaders: '${target}' is not a target")
    endif()
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "luaug_add_shaders(${target}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_GLOB)
        message(FATAL_ERROR "luaug_add_shaders(${target}): GLOB requires at least one pattern")
    endif()

    if(NOT LUAUG_SHADER_TOOLCHAIN)
        # ADR 0032: on a host with no DirectXShaderCompiler there is no HLSL
        # front-end, so the blobs come from a Tier-1/Tier-2 host instead. Said
        # out loud, because a silently empty content/shaders would be found much
        # later and much more expensively.
        message(STATUS
            "LuauG: no shader toolchain on this host -- ${target} gets no compiled shaders (ADR 0032)")
        return()
    endif()

    set(sources "")
    foreach(pattern IN LISTS arg_GLOB)
        file(GLOB matched CONFIGURE_DEPENDS "${pattern}")
        list(APPEND sources ${matched})
    endforeach()
    if(NOT sources)
        message(FATAL_ERROR
            "luaug_add_shaders(${target}): no .hlsl files matched ${arg_GLOB}")
    endif()
    # Sorted so the manifest is byte-identical across machines and filesystems.
    list(SORT sources)

    # Fixed by ADR 0006 ("shared code in shaders/include/*.hlsli") rather than
    # derived from the caller, so an `#include` resolves the same way no matter
    # which CMakeLists.txt asked for the compile.
    set(include_dir "${CMAKE_SOURCE_DIR}/shaders/include")
    set(include_args "")
    set(headers "")
    if(IS_DIRECTORY "${include_dir}")
        set(include_args -I "${include_dir}")
        file(GLOB headers CONFIGURE_DEPENDS "${include_dir}/*.hlsli")
        list(SORT headers)
    endif()

    # Resolved at configure time, because add_custom_command(OUTPUT) forbids
    # generator expressions -- $<TARGET_FILE_DIR:...> here is a configure error,
    # not a portability nicety.
    #
    # The obvious alternative, emitting under the binary dir and staging with a
    # POST_BUILD copy, is a trap: POST_BUILD only runs when the target relinks,
    # so editing only a shader would leave the staged copy stale and the engine
    # loading yesterday's blob. Emitting straight to the final location has no
    # such window.
    get_target_property(target_output_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT target_output_dir)
        get_target_property(target_output_dir ${target} BINARY_DIR)
    endif()
    if(target_output_dir MATCHES "\\$<")
        message(FATAL_ERROR
            "luaug_add_shaders(${target}): its output directory is a generator expression, "
            "which cannot be resolved here. Set a plain RUNTIME_OUTPUT_DIRECTORY on the target.")
    endif()

    set(out_dir "${target_output_dir}/content/shaders")
    set(outputs "")
    set(entries "")

    set(seen_names "")
    foreach(source IN LISTS sources)
        get_filename_component(name "${source}" NAME_WLE)

        # Output paths are flat per format, so two shaders sharing a stem in
        # different directories would silently overwrite each other's blobs.
        if(name IN_LIST seen_names)
            message(FATAL_ERROR
                "luaug_add_shaders(${target}): two shaders are both named '${name}'. "
                "Shader names are the manifest's keys and must be unique across the glob.")
        endif()
        list(APPEND seen_names "${name}")

        foreach(stage IN LISTS stages)
            set(entrypoint "${entry_${stage}}")
            set(format_lines "")

            foreach(format IN LISTS formats)
                set(relative "${format}/${name}.${stage}.${ext_${format}}")
                set(output "${out_dir}/${relative}")
                add_custom_command(
                    OUTPUT "${output}"
                    COMMAND shadercross
                        "${source}"
                        -s HLSL
                        -d ${dest_${format}}
                        -t ${stage}
                        -e ${entrypoint}
                        ${include_args}
                        -o "${output}"
                    DEPENDS shadercross "${source}" ${headers}
                    COMMENT "Shader ${name}.${stage} -> ${format}"
                    VERBATIM)
                list(APPEND outputs "${output}")
                list(APPEND format_lines "        \"${format}\": \"${relative}\"")
            endforeach()

            # SDL_GPU needs the resource counts at shader-creation time and the
            # input locations at pipeline-creation time; shadercross reflects
            # both out of the SPIR-V it just built. Emitting them here is what
            # keeps the runtime from hardcoding numbers that only the shader
            # source knows.
            set(reflect_relative "reflect/${name}.${stage}.json")
            set(reflect_output "${out_dir}/${reflect_relative}")
            add_custom_command(
                OUTPUT "${reflect_output}"
                COMMAND shadercross
                    "${source}"
                    -s HLSL
                    -d JSON
                    -t ${stage}
                    -e ${entrypoint}
                    ${include_args}
                    -o "${reflect_output}"
                DEPENDS shadercross "${source}" ${headers}
                COMMENT "Shader ${name}.${stage} -> reflection"
                VERBATIM)
            list(APPEND outputs "${reflect_output}")

            file(RELATIVE_PATH source_relative "${CMAKE_SOURCE_DIR}" "${source}")
            string(JOIN ",\n" format_block ${format_lines})
            list(APPEND entries
"    {
      \"name\": \"${name}\",
      \"stage\": \"${stage}\",
      \"entrypoint\": \"${entrypoint}\",
      \"source\": \"${source_relative}\",
      \"reflect\": \"${reflect_relative}\",
      \"formats\": {
${format_block}
      }
    }")
        endforeach()
    endforeach()

    string(JOIN ",\n" entries ${entries})

    # The manifest's content is fully known at configure time, but it is written
    # into the generated tree and copied into content/ by a real build rule
    # rather than straight into place: a file with no rule that produces it is a
    # file Ninja refuses to rebuild, so deleting `content/` -- the most obvious
    # way to force a clean shader build -- would wedge the build until the next
    # configure.
    set(manifest_source "${LUAUG_GENERATED_DIR}/shaders/${target}-manifest.json")
    file(GENERATE OUTPUT "${manifest_source}" CONTENT
"{
  \"version\": 1,
  \"comment\": \"GENERATED by cmake/luaug_shaders.cmake. Paths are relative to this file.\",
  \"shaders\": [
${entries}
  ]
}
")

    set(manifest "${out_dir}/manifest.json")
    add_custom_command(
        OUTPUT "${manifest}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${manifest_source}" "${manifest}"
        DEPENDS "${manifest_source}"
        COMMENT "Shader manifest for ${target}"
        VERBATIM)

    add_custom_target(${target}_shaders DEPENDS ${outputs} "${manifest}")
    add_dependencies(${target} ${target}_shaders)
endfunction()
