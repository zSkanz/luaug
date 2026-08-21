# L4 -- the mixer and the sound timeline (architecture.md §2 `audio`, ADR 0009).
#
# **At L4 rather than L2, from M6.** The sketch put it beside the other backend
# seams, and it owns the `Sound` and `AudioGroup` classes -- a module that
# registers a class into scene's registry has to be able to see scene, and L2
# cannot. ADR 0009's seam is untouched by the move: the seam is the module
# boundary, not its layer, and `render` sits here for exactly the same reason.
#
# miniaudio is compiled in ONE translation unit and appears in no header. That is
# what makes "the public API never leaks miniaudio concepts" structural: nothing
# outside src/audio.cpp can name a miniaudio type even by accident.

luaug_add_module(audio
    LAYER 4
    SOURCES
        src/audio.cpp
        src/native_accessors.cpp
        generated/class_descriptors.gen.cpp
    DEPS
        core
        scene
)

target_include_directories(luaug_audio SYSTEM PRIVATE "${LUAUG_THIRD_PARTY_DIR}/miniaudio")

luaug_add_module_tests(audio
    MODULES
        audio
    SOURCES
        tests/audio_tests.cpp
)
