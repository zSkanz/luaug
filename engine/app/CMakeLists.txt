# L6 -- the host. This is the one module allowed to see everything. The VM moved
# into `engine/script` in M2, as planned: what is left here is the world it runs
# in, the project it is mounted from, and the frame loop that drives it
# (`world_host.h`).

luaug_add_module(app
    LAYER 6
    SOURCES
        src/world_host.cpp
        src/frame_scheduler.cpp
        src/backends.cpp
        src/engine.cpp
        src/preserved.cpp
        src/project_config.cpp
        src/reload.cpp
        src/replay.cpp
        src/two_worlds.cpp
        src/bench.cpp
        src/screenshot.cpp
        src/soak.cpp
        src/ui_text.cpp
        src/ui_theme.cpp
        src/script_document.cpp
        src/script_syntax.cpp
        src/script_editor.cpp
        src/streaming_host.cpp
        src/debug_overlay.cpp
        src/inspector.cpp
        src/launcher.cpp
        src/launcher_run.cpp
        src/partition_cache.cpp
        src/picking.cpp
        src/editor.cpp
        src/content_tree.cpp
        src/icons.cpp
        src/dev_control.cpp
    DEPS
        asset
        audio
        core
        jobs
        platform
        rhi_api
        net_api
        input
        physics_api
        render
        scene
        script
        ui
    PUBLIC_DEPS
        luaug::luau
        luaug::build_info
)

# The icon ids, generated from `icons/default/theme.json`. Beside its source
# rather than under include/luaug/app/, for the reason `luaug_scene` gives about
# its own: "do not edit" is easier to obey when it applies to a whole folder.
target_include_directories(luaug_app PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/generated>)

# The backends themselves are linked ONLY here (ADR 0023): every other module
# sees the seam. Which ones exist is a build decision, so the link list is
# assembled from the same options backends.cpp switches on.
if(LUAUG_RHI_SDLGPU)
    target_link_libraries(luaug_app PRIVATE luaug::rhi_sdlgpu)
endif()
if(LUAUG_RHI_CAPTURE)
    target_link_libraries(luaug_app PRIVATE luaug::rhi_capture)
endif()
if(LUAUG_RHI_NULL)
    target_link_libraries(luaug_app PRIVATE luaug::rhi_null)
endif()

# The physics backend, for the same reason and by the same rule (ADR 0023): the
# seam is what `scene` and everything else sees, and the implementation is
# linked here alone.
if(LUAUG_PHYSICS_JOLT)
    target_link_libraries(luaug_app PRIVATE luaug::physics_jolt)
endif()

# The debug overlay (ADR 0011). Dev builds only: the option lives beside the
# dependency in third_party/CMakeLists.txt, and a shipping build declares no
# ImGui target at all -- so what is linked here IS the whole feature.
#
# SDL3 is named explicitly, for the same reason `platform` makes every consumer
# of sdl_interop.h name it. The overlay reaches the real SDL_Window and the real
# SDL_GPUDevice through two interop headers whose whole point is that crossing
# that line should be an edit to a CMakeLists, not something an #include can do
# on its own.
if(LUAUG_DEBUG_UI)
    target_link_libraries(luaug_app PRIVATE luaug::imgui SDL3::SDL3-static)
endif()

# PUBLIC rather than PRIVATE: debug_overlay.h describes a class that is inert in
# shipping, and a test asserting that has to be able to tell which build it is
# in. The header itself never reads this -- the class has the same shape in
# every profile, which is what keeps the frame loop free of #ifdefs.
target_compile_definitions(luaug_app PUBLIC LUAUG_DEBUG_UI=$<BOOL:${LUAUG_DEBUG_UI}>)


add_executable(luaug_host src/main.cpp)
set_target_properties(luaug_host PROPERTIES OUTPUT_NAME luaug-host)
target_link_libraries(luaug_host PRIVATE luaug::app luaug::warnings)

# The application's identity, embedded in the artifact rather than left beside
# it (roadmap M8). Windows only here because a Windows icon IS a PE resource;
# the macOS and Linux halves are a bundle's `Info.plist` and a `.desktop` entry,
# and both belong to a packaging step neither of this machine's two tiers can
# run. `luaug.rc` says what this is and why a game replaces it.
if(WIN32)
    target_sources(luaug_host PRIVATE "${CMAKE_SOURCE_DIR}/branding/luaug.rc")
    # So a change to the artwork rebuilds the binary that carries it. CMake does
    # not scan an .rc for its dependencies, and an icon that silently kept the
    # previous build's bytes is exactly the regression the read-back check
    # exists to catch -- it should never have to.
    set_property(SOURCE "${CMAKE_SOURCE_DIR}/branding/luaug.rc" APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_SOURCE_DIR}/branding/icon/luaug.ico")
endif()

# The catalog ships beside the binary as content, because the host resolves it
# relative to its own location rather than the source tree -- the same shape a
# packaged build uses (architecture.md §8).
add_custom_command(TARGET luaug_host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/i18n/en.json"
        "$<TARGET_FILE_DIR:luaug_host>/content/i18n/en.json"
    # `@luaug/…` ships as content for the same reason: a project reads the same
    # file its editor type-checks against, rather than a copy compiled into the
    # binary that nothing can inspect.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/runtime/luaug"
        "$<TARGET_FILE_DIR:luaug_host>/content/runtime/luaug"
    # The conformance runner, for the same reason again: it is an entry script
    # the headless runner mounts, and a real file is one the gate can analyse.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/runtime/conformance"
        "$<TARGET_FILE_DIR:luaug_host>/content/runtime/conformance"
    # The default typeface (roadmap M7, human decision 2026-08-20). Content
    # rather than an array compiled into the binary: it is 868 KB of data the
    # engine READS, a packaged build ships it beside the executable like every
    # other asset, and a byte array nobody can open is a byte array nobody can
    # check against upstream.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/inter/docs/font-files/InterVariable.ttf"
        "$<TARGET_FILE_DIR:luaug_host>/content/fonts/Inter.ttf"
    # The CODE face (ADR 0057). Cousine, which ImGui already vendors among its
    # sample assets, under the SIL OFL 1.1 -- the same licence Inter ships under
    # and one `tools/repo/licensecheck.luau` already admits. No new dependency
    # row: this is a file inside a tree the manifest already pins by commit, so
    # it cannot move without somebody bumping ImGui on purpose. Its credit is
    # declared through the manifest's `bundled` list, because
    # THIRD_PARTY_NOTICES.md is generated and a hand-added line would be
    # overwritten by the next `vendor.luau notices`.
    #
    # Inter is still the engine's typeface. A code editor set in a proportional
    # face is not a code editor, which is the whole of why there are two.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/imgui/misc/fonts/Cousine-Regular.ttf"
        "$<TARGET_FILE_DIR:luaug_host>/content/fonts/Mono.ttf"
    # The editor's icon themes. A plain recursive copy and deliberately NOT
    # through `assetc`: that path sends every image to basisu for KTX2/UASTC
    # with generated mips, which is right for a world texture and wrong for a
    # sixteen-pixel glyph -- block compression smears small alpha shapes, and a
    # small alpha shape is all an icon is. `icons/README.md` says so and says
    # why.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/icons"
        "$<TARGET_FILE_DIR:luaug_host>/content/icons"
    # The project templates and the generated definitions, so the LAUNCHER can
    # make a project (ADR 0055). `luaug new` reads them from the installation
    # root and this reads them from content, which is the same tree copied to two
    # places by `tools/repo/package.luau` -- and the test that compares what the
    # two scaffolders produce is what keeps that from being two answers.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/templates"
        "$<TARGET_FILE_DIR:luaug_host>/content/templates"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/runtime/types/engine.d.luau"
        "$<TARGET_FILE_DIR:luaug_host>/content/runtime/types/engine.d.luau"
    COMMENT "Staging the engine message catalog, the @luaug modules, the conformance runner, the editor icons and the project templates")

luaug_add_module_tests(app
    SOURCES
        tests/preserved_tests.cpp
        tests/reload_state_tests.cpp
        tests/reload_tests.cpp
        tests/world_host_tests.cpp
        tests/debug_overlay_tests.cpp
        tests/inspector_tests.cpp
        tests/launcher_tests.cpp
        tests/picking_tests.cpp
        tests/editor_tests.cpp
        tests/content_tree_tests.cpp
        tests/partition_cache_tests.cpp
        tests/soak_tests.cpp
        tests/two_worlds_tests.cpp
        tests/project_config_tests.cpp
        tests/ui_theme_tests.cpp
        tests/script_document_tests.cpp
        tests/script_editor_tests.cpp
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

    # M0's deliverable, still asserted: the host runs a script and prints a
    # catalog-resolved greeting.
    #
    # `--headless --frames=3 --exit` is not decoration. Since M1 the host runs a
    # frame loop, and without a budget it runs until its window is closed --
    # which under CTest means forever. This test hung for 140 seconds locally
    # before the flags went on, and would have burned the CI job's timeout.
    #
    # `--rhi=null` for the same reason: these tests are about the host, and a
    # Linux CI runner has no Vulkan driver. Making them depend on a GPU would
    # turn "this runner has no graphics" into a red build, which is a check
    # nobody can act on. The real device is covered by the rhi tests, which skip
    # when there is none, and by the Tier-1 gate run on the dev machine.
    add_test(NAME host_boot
        COMMAND luaug_host "${CMAKE_SOURCE_DIR}/examples/boot/boot.luau" --headless --frames=3 --exit --rhi=null)
    set_tests_properties(host_boot PROPERTIES
        PASS_REGULAR_EXPRESSION "engine initialized"
        TIMEOUT 60)

    # The frame loop terminates on its budget and says what it ran. Separate
    # from host_boot because "the script ran" and "the loop stopped" are
    # different claims, and a hang should name which one broke.
    add_test(NAME host_headless_frames COMMAND luaug_host --headless --frames=5 --exit --rhi=null)
    set_tests_properties(host_headless_frames PROPERTIES
        PASS_REGULAR_EXPRESSION "Ran 5 frames"
        TIMEOUT 60)

    # A headless run with no budget has no window to close, so it would never
    # stop. Rejected up front rather than left to a CI timeout to discover.
    add_test(NAME host_headless_needs_frames COMMAND luaug_host --headless)
    set_tests_properties(host_headless_needs_frames PROPERTIES WILL_FAIL TRUE TIMEOUT 60)

    # Selecting a backend this build does not contain names what it does have.
    add_test(NAME host_unknown_backend COMMAND luaug_host --rhi=nonesuch)
    set_tests_properties(host_unknown_backend PROPERTIES WILL_FAIL TRUE TIMEOUT 60)

    # **A host with no script used to refuse and now opens the project browser**
    # (ADR 0055), so what this asserts moved rather than disappeared: a path that
    # is not there is still a refusal, which is the case somebody reaches by
    # typo. Nothing here can drive the browser -- it needs a window -- and what
    # would go wrong quietly is a launcher that opened for a mistyped path
    # instead of saying so.
    #
    # A path that EXISTS and holds no scripts is deliberately not this test: the
    # host warns and runs it, which it has always done, because an empty project
    # is a project somebody is part-way through making.
    add_test(NAME host_refuses_a_path_that_is_not_there
        COMMAND luaug_host ${CMAKE_SOURCE_DIR}/no-such-project --rhi=null --headless --frames=1)
    set_tests_properties(host_refuses_a_path_that_is_not_there PROPERTIES WILL_FAIL TRUE TIMEOUT 60)

    # The M2 gate: ~900 Luau specs written against docs/api-design.md by authors
    # who were forbidden from reading `engine/` (MASTER_PROMPT §7). That is what
    # makes them a contract rather than a transcript, and it is why they are run
    # here rather than trusted.
    #
    # `--rhi=null` because the suite tests the kernel and not the renderer: a
    # machine with no GPU should still be able to prove the engine's semantics.
    add_test(NAME conformance
        COMMAND luaug_host --run-tests=${CMAKE_SOURCE_DIR}/tests/conformance --rhi=null)
    set_tests_properties(conformance PROPERTIES TIMEOUT 300)

    # The other M2 gate: same script and same seed, twice, 10,000 ticks,
    # identical world hash at every checkpoint (ADR 0025). No `--rhi` because a
    # replay opens no device at all -- if rendering could change the hash, that
    # is the defect, not a configuration.
    #
    # This process is fresh every time CTest starts it, so running it here is
    # also architecture.md §9's cross-process leg; the in-process repeat that
    # catches leaked global state happens inside the run.
    add_test(NAME determinism
        COMMAND luaug_host --replay=${CMAKE_SOURCE_DIR}/tests/determinism)
    set_tests_properties(determinism PROPERTIES TIMEOUT 300)

    # Three of M6's gate items, and all three are replays with a FACT to prove
    # rather than only a hash to compare. The scenario manifest names an
    # attribute the run has to end having set, because a hash alone cannot gate
    # any of them: three runs that all fall in the same hole agree perfectly.
    #
    #   obby      -- `examples/04-obby` driven by a recorded input stream from
    #                the menu to the finish flag. The whole stack is in the loop:
    #                input dispatch, UI layout and hit-testing, tweens driving
    #                kinematic platforms, physics, audio and animation. It is
    #                worth more than the five system gates it does not replace.
    #   audiosoak -- sixty seconds of continuous mixing with the underrun counter
    #                still zero, and the harder half: an exact, predictable
    #                number of `Ended` events off the SIM timeline. The
    #                real-device half is a windowed run recorded by hand, since
    #                only a machine with speakers can say the device opened.
    #   animation -- three clips at three speeds, their `TimePosition` driven into
    #                the world so the replay hash covers clip sampling at all.
    add_test(NAME replay_gates
        COMMAND luaug_host --replay=${CMAKE_SOURCE_DIR}/tests/replay)
    set_tests_properties(replay_gates PROPERTIES TIMEOUT 300)

    # The perf threshold (roadmap M2: the 10k-parts / 1k-listeners churn
    # benchmark "with a CI threshold", and the 500-instance gate scene). Both
    # budgets are deliberately loose -- see `bench.h`: a CI runner's speed varies
    # by more than the regression anyone would want to catch, so this fails on a
    # catastrophe and `docs/perf-baselines.md` holds the real numbers.
    #
    # One repeat here rather than the default three. The gate asks whether the
    # engine still works, and three runs of a 10,000-instance scene is thirty
    # seconds of CI for a number nobody reads.
    # M7's gate: "asset build determinism check in CI". Two PROCESSES rather
    # than the in-process double build `luaug_assetc_tests` already does -- see
    # the driver for what the second process can see that the second call
    # cannot. `examples/05-streaming` is the input because it is the largest
    # content tree in the repository: 289 chunks, meshes and textures.
    add_test(NAME asset_determinism
        COMMAND ${CMAKE_COMMAND}
            -DASSETC=$<TARGET_FILE:assetc>
            -DCONTENT=${CMAKE_SOURCE_DIR}/examples/05-streaming/content
            -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/asset-determinism
            -DGENERATOR=${CMAKE_SOURCE_DIR}/examples/05-streaming/tools/generate_world.luau
            -DREPO=${CMAKE_SOURCE_DIR}
            -DWORLD=${CMAKE_SOURCE_DIR}/examples/05-streaming/content/world
            -DMIN_FILES=290
            -P ${CMAKE_SOURCE_DIR}/tests/assetdeterminism/run_determinism_gate.cmake)
    set_tests_properties(asset_determinism PROPERTIES TIMEOUT 600)

    # M7's gate: the scripted five-minute fly-through over `examples/05-streaming`,
    # asserting peak memory under a declared ceiling and a frame-time histogram
    # with no hitch over 33 ms.
    #
    # Eighteen thousand frames is five minutes of SIM time and about ten seconds
    # of wall clock, because the fixed 1/60 s headless step decouples the two.
    # That is the whole reason a five-minute gate is affordable at all.
    #
    # THE THIRD ASSERTION IS THE ONE THAT EARNED ITS PLACE. D032 degraded from
    # 100 fps to 35 over five minutes with no hitch anywhere and no ceiling
    # breach -- a thousand instances leaked every fifteen seconds. `soak.h`
    # explains why the instance count flattening is the property that separates
    # a streamed world from a growing one.
    #
    # The ceiling is 192 MiB against 25 MiB measured. Loose rather than tight on
    # purpose: it is a backstop for a leak the growth check somehow misses, and a
    # ceiling with no headroom is a gate that goes red when an allocator changes.
    add_test(NAME streaming_soak
        COMMAND ${CMAKE_COMMAND}
            -DHOST=$<TARGET_FILE:luaug_host>
            -DASSETC=$<TARGET_FILE:assetc>
            -DPROJECT=${CMAKE_SOURCE_DIR}/examples/05-streaming
            -DGENERATOR=${CMAKE_SOURCE_DIR}/examples/05-streaming/tools/generate_world.luau
            -DREPO=${CMAKE_SOURCE_DIR}
            -DWORLD=${CMAKE_SOURCE_DIR}/examples/05-streaming/content/world
            -DREPORT=${CMAKE_CURRENT_BINARY_DIR}/streaming-soak.json
            -DFRAMES=18000
            -DCEILING_MB=192
            -DMIN_INSTANCES=1000
            -P ${CMAKE_SOURCE_DIR}/tests/streamsoak/run_soak_gate.cmake)
    set_tests_properties(streaming_soak PROPERTIES TIMEOUT 600)

    # M8's gate: the ten-minute soak over the flagship, at the length a test
    # suite can afford to run every time. **The full ten minutes is a milestone
    # gate step and is recorded in the brief**; this is a hundred seconds of
    # simulation, which is two whole autopilot cycles -- a walk leg and a flight
    # leg twice each.
    #
    # A hundred seconds is not an arbitrary length. The soak's flatten check
    # compares the second quarter of a run against the fourth, and the demo's
    # autopilot alternates between a walk and a flight every twenty-five
    # seconds -- so at this length both quarters land on the same leg, and the
    # comparison is between like and like rather than between a walk and a
    # flight.
    #
    # The ceiling is 384 MiB against 169 measured. Loose on purpose, as M7's
    # is: it is a backstop for a leak the growth check somehow misses, and a
    # ceiling with no headroom is a gate that goes red when an allocator
    # changes.
    add_test(NAME openworld_soak
        COMMAND ${CMAKE_COMMAND}
            -DHOST=$<TARGET_FILE:luaug_host>
            -DASSETC=$<TARGET_FILE:assetc>
            -DPROJECT=${CMAKE_SOURCE_DIR}/examples/10-open-world
            -DGENERATOR=${CMAKE_SOURCE_DIR}/examples/10-open-world/tools/generate_world.luau
            -DREPO=${CMAKE_SOURCE_DIR}
            # **A scene rather than a directory of chunk sources** (ADR 0053).
            # The flagship's generator used to write `content/world/*.chunk.json`
            # for `assetc` to compile; it writes a SCENE now, and the engine cuts
            # that into cells on the way to the first frame. What this gate has
            # to ensure exists therefore moved with it -- `EXISTS` answers for a
            # file exactly as it did for a directory, so the check itself did not
            # have to change.
            -DWORLD=${CMAKE_SOURCE_DIR}/examples/10-open-world/content/scenes/main.scene.json
            -DREPORT=${CMAKE_CURRENT_BINARY_DIR}/openworld-soak.json
            -DFRAMES=6000
            -DCEILING_MB=384
            -DMIN_INSTANCES=2000
            -P ${CMAKE_SOURCE_DIR}/tests/streamsoak/run_soak_gate.cmake)
    set_tests_properties(openworld_soak PROPERTIES TIMEOUT 900)

    add_test(NAME perf_budget
        COMMAND luaug_host --bench=${CMAKE_SOURCE_DIR}/tests/bench --bench-repeats=1)
    set_tests_properties(perf_budget PROPERTIES TIMEOUT 600 LABELS perf)

    # The blocking render-regression gate (architecture.md §9): record the
    # command stream and diff it byte-for-byte against a golden. No GPU, no
    # driver, no tolerance -- so a graphics vendor's update cannot turn the
    # merge queue red, and it runs on every tier including CI.
    if(LUAUG_RHI_CAPTURE)
        add_test(NAME capture_gate
            COMMAND ${CMAKE_COMMAND}
                -DHOST=$<TARGET_FILE:luaug_host>
                -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/rendercapture/clear-3frames.jsonl
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/capture-gate.jsonl
                -DSCRIPT=${CMAKE_SOURCE_DIR}/examples/00-clear/init.luau
                -DFRAMES=3
                -P ${CMAKE_SOURCE_DIR}/tests/rendercapture/run_capture_gate.cmake)
        set_tests_properties(capture_gate PROPERTIES TIMEOUT 60)

        # M4's gate: three camera angles by two lighting states, as six
        # consecutive frames of one stream. Every camera position and clock
        # value is a literal at a named tick -- an orbit driven by elapsed time
        # would make the golden depend on how many ticks had passed when it was
        # recorded, and a one-tick shift changes every number in a command
        # stream (M1 Finding 9, M3 Finding 15).
        add_test(NAME capture_gate_meshes
            COMMAND ${CMAKE_COMMAND}
                -DHOST=$<TARGET_FILE:luaug_host>
                -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/rendercapture/meshes-6frames.jsonl
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/capture-gate-meshes.jsonl
                -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/rendercapture/meshes
                -DFRAMES=6
                -P ${CMAKE_SOURCE_DIR}/tests/rendercapture/run_capture_gate.cmake)
        set_tests_properties(capture_gate_meshes PROPERTIES TIMEOUT 120)

        # M6's gate: the SAME UI script captured at two resolutions, as two
        # goldens. The pair is the check -- a layout that ignored the window
        # would produce two identical streams, and the two differ in exactly the
        # way scaling predicts: the clipped strip's scissor is 512,668,704x40 at
        # 1280x720 and 256,308,352x40 at 640x360, so the scale-driven numbers
        # halve and the offset-driven height does not.
        #
        # What these goldens can and cannot see is worth knowing before trusting
        # them: `upload` records a buffer's SIZE and not its contents (D026), so
        # the QUADS are invisible here. The rectangles themselves are asserted
        # exactly by `luaug_ui_tests`, and the pixels by a screenshot in the gate
        # record. This pair covers the scissors, the viewport and the draw count.
        foreach(resolution 1280x720 640x360)
            string(REPLACE "x" ";" size ${resolution})
            list(GET size 0 ui_width)
            list(GET size 1 ui_height)
            add_test(NAME capture_gate_ui_${resolution}
                COMMAND ${CMAKE_COMMAND}
                    -DHOST=$<TARGET_FILE:luaug_host>
                    -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/rendercapture/uipanel-${resolution}.jsonl
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/capture-gate-ui-${resolution}.jsonl
                    -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/rendercapture/uipanel
                    -DFRAMES=2
                    -DWIDTH=${ui_width}
                    -DHEIGHT=${ui_height}
                    -P ${CMAKE_SOURCE_DIR}/tests/rendercapture/run_capture_gate.cmake)
            set_tests_properties(capture_gate_ui_${resolution} PROPERTIES TIMEOUT 60)
        endforeach()

        # The skinned gate (M6). Four frames of one animated mesh beside one that
        # is not animated at all, which is what makes it able to tell "the
        # skinned path ran" from "everything went through the skinned path".
        #
        # Unlike the UI pair, this golden CAN see the thing it is about: the
        # joint palette goes up as a uniform block and `bindUniforms` records
        # contents (the half of D026 that was fixed at M4.5), so a pose that
        # stopped moving is four identical lines in a file the diff points at.
        add_test(NAME capture_gate_skinned
            COMMAND ${CMAKE_COMMAND}
                -DHOST=$<TARGET_FILE:luaug_host>
                -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/rendercapture/skinned-4frames.jsonl
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/capture-gate-skinned.jsonl
                -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/rendercapture/skinned
                -DFRAMES=4
                -P ${CMAKE_SOURCE_DIR}/tests/rendercapture/run_capture_gate.cmake)
        set_tests_properties(capture_gate_skinned PROPERTIES TIMEOUT 60)

        # M4.5's gate addition, and the one check in this file that is not a
        # golden: one scene at two `ClockTime` values, and the two frames must
        # differ. See the driver for why a golden could not make that claim --
        # and did not, for the whole of M4.
        add_test(NAME clock_differential
            COMMAND ${CMAKE_COMMAND}
                -DHOST=$<TARGET_FILE:luaug_host>
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/clock-differential.jsonl
                -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/rendercapture/clockpair
                -P ${CMAKE_SOURCE_DIR}/tests/rendercapture/run_clock_differential.cmake)
        set_tests_properties(clock_differential PROPERTIES TIMEOUT 120)
    endif()

    # The contact-shadow golden (D051), and it exists because the artifact was
    # invisible at every framing this repository already had. A human sent a
    # close-up of a capsule and a boulder with a band of lit floor between each
    # object and its own shadow -- both hovering -- and the same objects in
    # `tests/screenshots/specular` show that gap as about one pixel.
    #
    # Frame 15 puts the sun at half past nine, which is where a detached contact
    # is most obvious: high enough that each shadow sits beside its object rather
    # than stretching to the horizon, low enough that a centimetre of detachment
    # is a centimetre of bright floor. The scene sweeps the hour with the frame
    # number, so the two ends of the range are one flag away.
    #
    # `gpu-golden` like the other two: this frame is edges and soft gradients,
    # and neither survives a move to a software rasterizer.
    add_test(NAME screenshot_gate_contact
        COMMAND ${CMAKE_COMMAND}
            -DHOST=$<TARGET_FILE:luaug_host>
            -DIMGCMP=$<TARGET_FILE:imgcmp>
            -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/screenshots/contact-0930.png
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/screenshot-gate-contact.png
            -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/screenshots/contact
            -DFRAMES=15
            -P ${CMAKE_SOURCE_DIR}/tests/screenshots/run_screenshot_gate.cmake)
    set_tests_properties(screenshot_gate_contact PROPERTIES
        LABELS "gpu-golden"
        SKIP_REGULAR_EXPRESSION "LUAUG_TEST_SKIP"
        TIMEOUT 120)

    # M8's graphics settings family, as a differential. See the driver for why
    # the claim is "these two must differ" rather than a golden: a preset that
    # is parsed and then reaches nothing renders exactly what it did before.
    add_test(NAME graphics_settings_differential
        COMMAND ${CMAKE_COMMAND}
            -DHOST=$<TARGET_FILE:luaug_host>
            -DIMGCMP=$<TARGET_FILE:imgcmp>
            -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/screenshots/specular
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/settings-differential
            -DFRAMES=120
            -P ${CMAKE_SOURCE_DIR}/tests/screenshots/run_settings_differential.cmake)
    set_tests_properties(graphics_settings_differential PROPERTIES
        SKIP_REGULAR_EXPRESSION "LUAUG_TEST_SKIP"
        TIMEOUT 180)

    # M8's editor-seam proof, and ADR 0017's standing condition coming due: two
    # `WorldHost`s alive at once, each with its own `ScriptRuntime` -- two Luau
    # VMs -- rendered into two targets.
    #
    # Not a `gpu-golden`: there is no checked-in image and no reference GPU. What
    # it compares is this machine's own renders against each other, which is why
    # it is a gate everywhere rather than only where the golden was recorded. It
    # skips where there is no device, like every other test that needs one.
    add_test(NAME editor_seam
        COMMAND ${CMAKE_COMMAND}
            -DHOST=$<TARGET_FILE:luaug_host>
            -DROOT=${CMAKE_SOURCE_DIR}/tests/twoworlds
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/two-worlds
            -P ${CMAKE_SOURCE_DIR}/tests/twoworlds/run_two_worlds_gate.cmake)
    set_tests_properties(editor_seam PROPERTIES
        SKIP_REGULAR_EXPRESSION "LUAUG_TEST_SKIP"
        TIMEOUT 180)

    # The M1 gate's reference screenshot: render a frame on a real GPU, read it
    # back, and compare against a checked-in golden with tools/imgcmp.
    #
    # This is the milestone's actual product -- from here on, any change with
    # visible output is verifiable by a machine rather than by looking at it
    # (MASTER_PROMPT.md §8, the observation rule). It skips rather than fails
    # where there is no GPU; see the driver script for why.
    add_test(NAME screenshot_gate
        COMMAND ${CMAKE_COMMAND}
            -DHOST=$<TARGET_FILE:luaug_host>
            -DIMGCMP=$<TARGET_FILE:imgcmp>
            -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/screenshots/pulse-frame30.png
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/screenshot-gate.png
            -DSCRIPT=${CMAKE_SOURCE_DIR}/examples/00-clear/init.luau
            -DFRAMES=30
            -P ${CMAKE_SOURCE_DIR}/tests/screenshots/run_screenshot_gate.cmake)
    # Labelled `gpu-golden` and EXCLUDED from CI (`ctest -LE gpu-golden`).
    #
    # A pixel golden is tied to the GPU that recorded it. Flat colour survived
    # the move -- tolerance 2 covers a unorm rounding difference -- but geometry
    # does not: line rasterization rules differ between a discrete GPU and the
    # runner's software rasterizer, and the gate went red on Windows CI the
    # moment the frame contained edges.
    #
    # The tempting fix is to widen the tolerance until it passes on both, which
    # would leave a gate that cannot see a real change either. architecture.md
    # §9 already says the other thing: the command stream is the BLOCKING gate
    # because it needs no GPU, and real-image comparison is the agent's own
    # verification tool -- run where the golden was recorded, which the roadmap
    # explicitly sanctions ("a scripted local gate on the dev machine, recorded
    # in the gate log either way").
    #
    # It runs on every `scripts/localgate.ps1`, and this label is what keeps it
    # from becoming a gate nobody trusts.
    set_tests_properties(screenshot_gate PROPERTIES
        LABELS "gpu-golden"
        SKIP_REGULAR_EXPRESSION "LUAUG_TEST_SKIP"
        TIMEOUT 120)

    # The INSTANCED path's golden, and it exists because that path shipped at
    # M7.5 drawing nothing at all (D043). Every gate the milestone had was
    # satisfied: the draw-call count fell from 15,250 to 22, the command stream
    # matched its capture, and the frame got eight times faster -- because the
    # geometry was being transformed off-screen. **A command stream and a
    # counter can both be right about a frame that is empty**, and the only
    # instrument that can tell is one that looks at pixels.
    #
    # `tests/screenshots/specular` is eight identical polished spheres, which is
    # a batch of eight and therefore the instanced path, plus a floor that is
    # not. It is also the evidence scene for geometric specular antialiasing --
    # one scene, because both claims are about what a curved metal surface
    # LOOKS like.
    #
    # `gpu-golden` for the same reason `screenshot_gate` is: this frame has
    # edges, and edges do not survive a move to a software rasterizer.
    add_test(NAME screenshot_gate_instanced
        COMMAND ${CMAKE_COMMAND}
            -DHOST=$<TARGET_FILE:luaug_host>
            -DIMGCMP=$<TARGET_FILE:imgcmp>
            -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/screenshots/specular-frame120.png
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/screenshot-gate-instanced.png
            -DSCRIPT=${CMAKE_SOURCE_DIR}/tests/screenshots/specular
            -DFRAMES=120
            -P ${CMAKE_SOURCE_DIR}/tests/screenshots/run_screenshot_gate.cmake)
    set_tests_properties(screenshot_gate_instanced PROPERTIES
        LABELS "gpu-golden"
        SKIP_REGULAR_EXPRESSION "LUAUG_TEST_SKIP"
        TIMEOUT 180)
endif()
