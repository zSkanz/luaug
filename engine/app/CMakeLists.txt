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
        src/reload.cpp
        src/replay.cpp
        src/bench.cpp
        src/screenshot.cpp
        src/debug_overlay.cpp
    DEPS
        core
        platform
        rhi_api
        render
        scene
        script
    PUBLIC_DEPS
        luaug::luau
        luaug::build_info
)

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

# stb_image_write, for --screenshot. Header-only and SYSTEM, so its warnings are
# not ours. Moves to `asset` when image IO becomes a subsystem (M4).
target_include_directories(luaug_app SYSTEM PRIVATE "${LUAUG_THIRD_PARTY_DIR}/stb")

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
    # `@luaug/…` ships as content for the same reason: a project reads the same
    # file its editor type-checks against, rather than a copy compiled into the
    # binary that nothing can inspect.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/runtime/luaug"
        "$<TARGET_FILE_DIR:luaug_host>/content/runtime/luaug"
    COMMENT "Staging the engine message catalog and the @luaug modules")

luaug_add_module_tests(app
    SOURCES
        tests/preserved_tests.cpp
        tests/reload_state_tests.cpp
        tests/reload_tests.cpp
        tests/world_host_tests.cpp
        tests/debug_overlay_tests.cpp
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

    add_test(NAME host_usage_without_script COMMAND luaug_host)
    set_tests_properties(host_usage_without_script PROPERTIES WILL_FAIL TRUE TIMEOUT 60)

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

    # The perf threshold (roadmap M2: the 10k-parts / 1k-listeners churn
    # benchmark "with a CI threshold", and the 500-instance gate scene). Both
    # budgets are deliberately loose -- see `bench.h`: a CI runner's speed varies
    # by more than the regression anyone would want to catch, so this fails on a
    # catastrophe and `docs/perf-baselines.md` holds the real numbers.
    #
    # One repeat here rather than the default three. The gate asks whether the
    # engine still works, and three runs of a 10,000-instance scene is thirty
    # seconds of CI for a number nobody reads.
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
    endif()

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
endif()
