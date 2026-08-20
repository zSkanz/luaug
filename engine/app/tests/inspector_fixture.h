// The class hierarchy the DebugShell tests inspect, in a header because two
// suites need the same one: `inspector_tests.cpp` asserts on what the sweep
// decides, and `debug_overlay_tests.cpp` drives the real ImGui panel over it on
// a real device.
//
// It is a hierarchy `engine/app` has never seen, declaring one property of
// **every** `ValueType` plus two read-only ones. That is deliberate: a generic
// sweep that has only ever swept the classes it was written against has not
// been shown to be generic (M4 brief, entering risk 6), and the one `ValueType`
// no shipped class uses is the one the panel is likeliest to forget.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/value.h"
#include "luaug/scene/world.h"

namespace luaug::app::testing
{

namespace core = luaug::core;
namespace scene = luaug::scene;

// If an alternative is ever appended to `Value`, this file is wrong and should
// say so at build time: the list of types the panel must render is exactly the
// list of alternatives, and a new one that nothing here names is the silent
// gap risk 6 describes.
static_assert(std::variant_size_v<scene::Value> == 9, "every Value alternative needs an editor and a test row");

// Where the fixture's properties actually live. Generated accessors are plain
// function pointers with no place to put state, so the real ones write into
// `World`'s component pools; a fixture that invented a pool would be testing a
// `World` nobody ships, so this keeps its values beside the world instead.
struct Bag
{
    bool flag = false;
    core::f64 count = 0.0;
    std::string label;
    core::Vec3 offset;
    core::CFrameD frame;
    core::Color3 tint;
    core::InstanceId link;
    scene::EnumValue mood;
    core::f64 sealed = 13.0;
};

inline std::unordered_map<core::u32, Bag> g_bags;

[[nodiscard]] inline Bag& bagOf(core::InstanceId id)
{
    return g_bags[id.index];
}

inline scene::Value getFlag(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).flag};
}

inline bool setFlag(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const bool* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    bagOf(id).flag = *flag;
    return true;
}

// Read-only, and therefore without a setter at all -- the shape the generator
// emits, because a setter nothing may call is a function nobody should write.
inline scene::Value getLocked(const scene::World&, core::InstanceId)
{
    return scene::Value{core::f64{42.0}};
}

// Read-only WITH a working setter. The generator emits read-only properties
// without one, so a panel that only ever checked `set == nullptr` would look
// correct against every class in the surface -- and `PropertyDesc` can express
// this combination, `World::setProperty` checks `readOnly` first, and a mutation
// proved the earlier test could not tell the two apart.
inline scene::Value getSealed(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).sealed};
}

inline bool setSealed(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::f64* sealed = std::get_if<core::f64>(&value);
    if (sealed == nullptr)
        return false;
    bagOf(id).sealed = *sealed;
    return true;
}

inline scene::Value getCount(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).count};
}

inline bool setCount(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::f64* count = std::get_if<core::f64>(&value);
    // A domain, so that `InvalidValue` is reachable at all: a rejection the
    // panel cannot provoke is a rejection the panel cannot be shown to report.
    if (count == nullptr || *count < 0.0)
        return false;
    bagOf(id).count = *count;
    return true;
}

inline scene::Value getLabel(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).label};
}

inline bool setLabel(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const std::string* label = std::get_if<std::string>(&value);
    if (label == nullptr)
        return false;
    bagOf(id).label = *label;
    return true;
}

inline scene::Value getOffset(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).offset};
}

inline bool setOffset(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::Vec3* offset = std::get_if<core::Vec3>(&value);
    if (offset == nullptr)
        return false;
    bagOf(id).offset = *offset;
    return true;
}

inline scene::Value getFrame(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).frame};
}

inline bool setFrame(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::CFrameD* frame = std::get_if<core::CFrameD>(&value);
    if (frame == nullptr)
        return false;
    bagOf(id).frame = *frame;
    return true;
}

inline scene::Value getTint(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).tint};
}

inline bool setTint(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::Color3* tint = std::get_if<core::Color3>(&value);
    if (tint == nullptr)
        return false;
    bagOf(id).tint = *tint;
    return true;
}

inline scene::Value getLink(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).link};
}

inline bool setLink(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::InstanceId* link = std::get_if<core::InstanceId>(&value);
    if (link == nullptr)
        return false;
    bagOf(id).link = *link;
    return true;
}

inline scene::Value getMood(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).mood};
}

inline bool setMood(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const scene::EnumValue* mood = std::get_if<scene::EnumValue>(&value);
    if (mood == nullptr)
        return false;
    bagOf(id).mood = *mood;
    return true;
}

// `ValueType::Nil`. Nothing in the v1 surface declares one, which is exactly
// why the fixture does: the type the panel is likeliest to forget is the one no
// shipped class exercises.
inline scene::Value getNothing(const scene::World&, core::InstanceId)
{
    return scene::Value{};
}

// A hierarchy `engine/app` has never seen, with one property of every
// `ValueType` and one that is read-only. Non-movable: the registry holds spans
// into the vectors below, so relocating one leaves every descriptor pointing at
// freed storage -- the same rule `scene`'s own fixture states.
struct Fixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;

    scene::ClassId thingClass = scene::InvalidClass;
    scene::ClassId widgetClass = scene::InvalidClass;
    scene::EnumId moodEnum = scene::InvalidEnum;

    Fixture()
    {
        // Instance ids are slot indices and restart from zero in every world,
        // so a bag left behind by the previous test would be read as this
        // world's state.
        g_bags.clear();

        moodItems = {
            scene::EnumItemDesc{.name = atoms.intern("Calm"), .value = 0, .docKey = {}},
            scene::EnumItemDesc{.name = atoms.intern("Cross"), .value = 7, .docKey = {}},
        };
        moodEnum = enums.registerEnum({
            .name = atoms.intern("Mood"),
            .docKey = {},
            .items = moodItems,
        });

        thingProperties = {
            scene::PropertyDesc{
                .name = atoms.intern("Flag"),
                .type = scene::ValueType::Bool,
                .get = &getFlag,
                .set = &setFlag,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Locked"),
                .type = scene::ValueType::Number,
                .readOnly = true,
                .get = &getLocked,
                .set = nullptr,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Sealed"),
                .type = scene::ValueType::Number,
                .readOnly = true,
                .get = &getSealed,
                .set = &setSealed,
            },
        };

        widgetProperties = {
            scene::PropertyDesc{
                .name = atoms.intern("Count"),
                .type = scene::ValueType::Number,
                .get = &getCount,
                .set = &setCount,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Label"),
                .type = scene::ValueType::String,
                .get = &getLabel,
                .set = &setLabel,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Offset"),
                .type = scene::ValueType::Vector3,
                .get = &getOffset,
                .set = &setOffset,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Frame"),
                .type = scene::ValueType::CFrame,
                .get = &getFrame,
                .set = &setFrame,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Tint"),
                .type = scene::ValueType::Color3,
                .get = &getTint,
                .set = &setTint,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Link"),
                .type = scene::ValueType::Instance,
                .get = &getLink,
                .set = &setLink,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Mood"),
                .type = scene::ValueType::EnumItem,
                .get = &getMood,
                .set = &setMood,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Nothing"),
                .type = scene::ValueType::Nil,
                .get = &getNothing,
                .set = nullptr,
            },
        };

        thingClass = classes.registerClass({
            .name = atoms.intern("Thing"),
            .defaultName = atoms.intern("Thing"),
            .properties = thingProperties,
        });
        widgetClass = classes.registerClass({
            .name = atoms.intern("Widget"),
            .super = thingClass,
            .defaultName = atoms.intern("Widget"),
            .properties = widgetProperties,
        });
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    [[nodiscard]] core::NameAtom atom(std::string_view text) { return atoms.intern(text); }

    [[nodiscard]] core::InstanceId widget(scene::World& world, std::string_view instanceName)
    {
        const core::InstanceId id = world.create(widgetClass);
        world.setName(id, atoms.intern(instanceName));
        return id;
    }

private:
    std::vector<scene::EnumItemDesc> moodItems;
    std::vector<scene::PropertyDesc> thingProperties;
    std::vector<scene::PropertyDesc> widgetProperties;
};

} // namespace luaug::app::testing
