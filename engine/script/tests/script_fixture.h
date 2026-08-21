// A booted VM over a world built from the GENERATED reflection tables.
//
// Deliberately not the hand-built hierarchy `scene`'s tests use: these files
// exist to check that a script reaches the real surface, and a fixture that
// declared its own classes would be checking the fixture. If a property is
// missing from `api/defs/*.api.luau`, these tests are where that shows up.
//
// Every assertion below runs INSIDE the VM. Comparing the C++ side against
// itself would prove nothing about what a script can reach, which is the only
// question these files exist to answer (M2 brief, ruling R-D).
#pragma once

#include "luaug/audio/scene_types.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/phase.h"
#include "luaug/input/scene_types.h"
#include "luaug/render/scene_types.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"
#include "luaug/script/runtime.h"

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "class_descriptors.gen.h"

namespace luaug::script::testing {

using core::usize;

struct Fixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;

    Fixture()
    {
        // Loaded because an error is identified by its `[key]` prefix and the
        // prefix is the CATALOG's name for the key -- without it every raise
        // reports `[i18n:missing:xxxxxxxx]` and `raises` below would be
        // asserting on nothing.
        catalogLoaded = static_cast<bool>(core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG));

        scene::generated::registerClasses(classes, atoms);
        // The other modules' classes as well, and only because `script` BINDS
        // methods on them: `InputAction:GetState` is registered from
        // services.cpp whether or not the class exists, and the boot cross-check
        // counts a binding whose class nothing declared as a mismatch. `render`
        // joined the list at M6, when `AnimationPlayer:LoadAnimation` became its
        // first class with a method; `ui` is still absent, and can be.
        input::registerSceneTypes(classes, atoms);
        audio::registerSceneTypes(classes, atoms);
        render::registerSceneTypes(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
        world.emplace(classes, enums, atoms, 1234u);
        runtime.emplace(*world);
        booted = !runtime->boot().has_value();

        // A script that yields and fails *after* being resumed reports through
        // the log rather than through a return value -- an error in a handler or
        // a task is contained by design (api-design.md §3.1). Capturing the log
        // is the only way a test can see one, and a test that could not see one
        // would pass for every assertion made after the first `task.wait`.
        //
        // The sink is process-global, so two live fixtures fight over it and the
        // younger wins. No test below keeps two alive while asserting on output.
        core::setLogSink(
            [this](core::LogLevel level, std::string_view text) { logged.emplace_back(level, std::string(text)); });
    }

    ~Fixture() { core::resetLogSink(); }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    std::vector<std::pair<core::LogLevel, std::string>> logged;

    // Constructed after the registries are populated, because
    // `registerInstanceBinding` resolves every method against them at boot.
    std::optional<scene::World> world;
    std::optional<ScriptRuntime> runtime;
    bool booted = false;
    bool catalogLoaded = false;

    [[nodiscard]] std::optional<core::EngineError> run(std::string_view source)
    {
        return runtime->runSource(source, "test");
    }

    // The chunk's own `assert` calls are the assertions; this only reports
    // whether one of them fired.
    [[nodiscard]] std::string failure(std::string_view source)
    {
        const std::optional<core::EngineError> error = run(source);
        return error.has_value() ? error->message : std::string{};
    }

    // Matches on the `[key]` prefix `core::makeError` writes, which is the
    // stable identifier a conformance spec is allowed to depend on -- the prose
    // after it is free to be translated or reworded (ADR 0019).
    [[nodiscard]] bool raises(std::string_view source, std::string_view key)
    {
        const std::optional<core::EngineError> error = run(source);
        if (!error.has_value())
            return false;
        return error->message.find(std::string("[").append(key).append("]")) != std::string::npos;
    }

    // Everything logged at `Error`, joined. Empty is the assertion that nothing
    // a handler or a task did after resuming went wrong.
    [[nodiscard]] std::string errors() const
    {
        std::string out;
        for (const auto& [level, text] : logged) {
            if (level != core::LogLevel::Error)
                continue;
            if (!out.empty())
                out.push_back('\n');
            out.append(text);
        }
        return out;
    }

    [[nodiscard]] std::size_t logCount(std::string_view needle) const
    {
        std::size_t count = 0;
        for (const auto& [level, text] : logged) {
            (void)level;
            if (text.find(needle) != std::string::npos)
                ++count;
        }
        return count;
    }

    [[nodiscard]] bool logContains(std::string_view needle) const
    {
        for (const auto& [level, text] : logged) {
            (void)level;
            if (text.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

    // One frame, in the shape architecture.md §3 describes: the sim clock
    // advances, a resumption point drains, the task-resume phase runs between
    // `PostSimulation` and `Heartbeat`, and whatever it deferred drains at
    // `Heartbeat`. The scheduler in `app` will drive exactly this; until it
    // exists the tests are the only driver, and driving it by hand is what makes
    // `task.wait` observable from a C++ test at all.
    void tick(usize count = 1)
    {
        for (usize index = 0; index < count; ++index) {
            scene::EngineState& state = world->engineState();
            state.tick += 1;
            state.simTime = static_cast<core::f64>(state.tick) * state.fixedTimestep;

            // Each resumption point runs its engine phase, then drains
            // (api-design.md §3.1). `PreRender` is render-rate and never fires
            // headless -- headless is the same scheduler minus the render steps.
            const core::f64 delta = state.fixedTimestep;
            for (const core::Phase phase :
                 {core::Phase::PreAnimation, core::Phase::PreSimulation, core::Phase::PostSimulation}) {
                runtime->firePhase(phase, delta);
                runtime->drain(phase);
            }

            runtime->resumeTimers();
            runtime->firePhase(core::Phase::Heartbeat, delta);
            runtime->drain(core::Phase::Heartbeat);
        }
    }
};

} // namespace luaug::script::testing
