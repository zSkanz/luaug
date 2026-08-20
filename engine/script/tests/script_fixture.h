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

#include <optional>
#include <string>
#include <string_view>

#include "class_descriptors.gen.h"
#include "luaug/core/i18n.h"
#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"
#include "luaug/script/runtime.h"

namespace luaug::script::testing
{

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
        scene::generated::registerEnums(enums, atoms);
        world.emplace(classes, enums, atoms, 1234u);
        runtime.emplace(*world);
        booted = !runtime->boot().has_value();
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

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
};

} // namespace luaug::script::testing
