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

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/value.h"
#include "luaug/scene/world.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace luaug::app::testing {

namespace core = luaug::core;
namespace scene = luaug::scene;

// If an alternative is ever appended to `Value`, this file is wrong and should
// say so at build time: the list of types the panel must render is exactly the
// list of alternatives, and a new one that nothing here names is the silent
// gap risk 6 describes.
static_assert(std::variant_size_v<scene::Value> == 13, "every Value alternative needs an editor and a test row");

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
    // **M6's screen-space four, which this fixture claimed to have and did
    // not.** The header above has said "one property of every `ValueType`"
    // since M4 and the `static_assert` below counts the alternatives, but
    // counting is not covering: `Vector2`, `UDim`, `UDim2` and `Rect` were
    // appended to the variant by M6 and no property here ever named them, so
    // four editor branches were executed by no test at all. Found while
    // rewriting the `UDim` widget, which is exactly the moment risk 6 predicted.
    core::Vec2 anchor;
    core::UDim pad;
    core::UDim2 extent;
    core::Rect slice;
};

inline std::unordered_map<core::u32, Bag> g_bags;

[[nodiscard]] inline Bag& bagOf(core::InstanceId id)
{
    return g_bags[id.index];
}

// Declared as an Instance reference and always absent, which is the pairing the
// real surface has and this fixture did not: `Instance.Parent` is nil on exactly
// one instance in a real world, the DataModel, and a human found it by clicking
// `go` on `RunService.Parent`. `editorFor` answers from the DECLARED type, so
// every editor branch reached for `InstanceId` and `std::get` threw.
//
// Kept as its own property rather than as a nil-able version of an existing one:
// the panel must survive a type it cannot draw an editor for, and that is only
// asserted if something in the surface is permanently in that state.
inline scene::Value getNilReference(const scene::World&, core::InstanceId)
{
    return scene::Value{};
}

// `BasePart.Material`, over the real component -- the same field the shipped
// accessor writes, so what a test asserts here is what a part actually carries.
//
// **The class check is the setter and not the picker.** A list that offers only
// materials is a convenience; refusing a `Part` where a `Material` belongs is
// the rule, and a UI is not where a rule lives.
inline scene::Value getPartMaterial(const scene::World& world, core::InstanceId id)
{
    const scene::PartComponent* part = world.parts().find(id);
    return part != nullptr && part->material.valid() ? scene::Value{part->material} : scene::Value{};
}

inline bool setPartMaterial(scene::World& world, core::InstanceId id, const scene::Value& value)
{
    scene::PartComponent* part = world.parts().find(id);
    if (part == nullptr)
        return false;
    if (std::holds_alternative<std::monostate>(value)) {
        part->material = core::InstanceId{};
        return true;
    }
    const core::InstanceId* named = std::get_if<core::InstanceId>(&value);
    if (named == nullptr)
        return false;
    if (named->valid() && world.materials().find(*named) == nullptr)
        return false;
    part->material = *named;
    return true;
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

inline scene::Value getAnchor(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).anchor};
}

inline bool setAnchor(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::Vec2* anchor = std::get_if<core::Vec2>(&value);
    if (anchor == nullptr)
        return false;
    bagOf(id).anchor = *anchor;
    return true;
}

inline scene::Value getPad(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).pad};
}

inline bool setPad(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::UDim* pad = std::get_if<core::UDim>(&value);
    if (pad == nullptr)
        return false;
    bagOf(id).pad = *pad;
    return true;
}

inline scene::Value getExtent(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).extent};
}

inline bool setExtent(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::UDim2* extent = std::get_if<core::UDim2>(&value);
    if (extent == nullptr)
        return false;
    bagOf(id).extent = *extent;
    return true;
}

inline scene::Value getSlice(const scene::World&, core::InstanceId id)
{
    return scene::Value{bagOf(id).slice};
}

inline bool setSlice(scene::World&, core::InstanceId id, const scene::Value& value)
{
    const core::Rect* slice = std::get_if<core::Rect>(&value);
    if (slice == nullptr)
        return false;
    bagOf(id).slice = *slice;
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
    // Unrelated to the other two by design: what a multi-selection has to
    // survive is two classes that share a NAME and nothing else. `Flag` is the
    // same type here and read-only, and `Count` is a string where the widget's
    // is a number -- one row is legal and the other cannot exist.
    scene::ClassId gadgetClass = scene::InvalidClass;
    // **The two classes a prefab STAGE needs**, and nothing more (ADR 0049).
    // A stage builds a `Workspace` to hold what is being edited and a
    // `Lighting` to see it by, and it finds them by name -- so a fixture
    // without them cannot stand one up. No components and no properties:
    // what the stage needs from these is that they exist.
    scene::ClassId workspaceClass = scene::InvalidClass;
    scene::ClassId partClass = scene::InvalidClass;
    scene::ClassId materialClass = scene::InvalidClass;
    scene::ClassId cameraClass = scene::InvalidClass;
    scene::ClassId pointLightClass = scene::InvalidClass;
    scene::ClassId lightingClass = scene::InvalidClass;
    // The content tree's root is a `Folder`, because that is what it is: a
    // place to put things (ADR 0052).
    scene::ClassId folderClass = scene::InvalidClass;
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
                .name = atoms.intern("Owner"),
                .type = scene::ValueType::Instance,
                .readOnly = true,
                .get = &getNilReference,
                .set = nullptr,
            },
            // Documented on the BASE class, so that the sweep is shown to carry
            // prose through the inheritance walk and not only off the leaf.
            scene::PropertyDesc{
                .name = atoms.intern("Flag"),
                .type = scene::ValueType::Bool,
                .doc = "Whether the thing is flagged.",
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
            // Names its enum and carries prose, which is what the generator
            // emits for a real one: the panel has to be able to answer "what
            // does this accept" and "what is this for" from the descriptor
            // alone, with no instance in hand.
            scene::PropertyDesc{
                .name = atoms.intern("Mood"),
                .type = scene::ValueType::EnumItem,
                .enumName = atoms.intern("Mood"),
                .doc = "How the widget feels about being inspected.",
                .get = &getMood,
                .set = &setMood,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Anchor"),
                .type = scene::ValueType::Vector2,
                .get = &getAnchor,
                .set = &setAnchor,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Pad"),
                .type = scene::ValueType::UDim,
                .doc = "A scale and an offset, which is the pairing the panel has to LABEL rather than merely draw.",
                .get = &getPad,
                .set = &setPad,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Extent"),
                .type = scene::ValueType::UDim2,
                .get = &getExtent,
                .set = &setExtent,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Slice"),
                .type = scene::ValueType::Rect,
                .get = &getSlice,
                .set = &setSlice,
            },
            // Deliberately carries neither, because a hand-built descriptor is
            // allowed to: `doc` has to read as an empty string rather than as a
            // null pointer, and the enum lookup has to answer "no enum" rather
            // than reach for one.
            scene::PropertyDesc{
                .name = atoms.intern("Nothing"),
                .type = scene::ValueType::Nil,
                .get = &getNothing,
                .set = nullptr,
            },
        };

        gadgetProperties = {
            scene::PropertyDesc{
                .name = atoms.intern("Flag"),
                .type = scene::ValueType::Bool,
                .readOnly = true,
                .get = &getFlag,
                .set = nullptr,
            },
            scene::PropertyDesc{
                .name = atoms.intern("Count"),
                .type = scene::ValueType::String,
                .get = &getLabel,
                .set = &setLabel,
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
        gadgetClass = classes.registerClass({
            .name = atoms.intern("Gadget"),
            .defaultName = atoms.intern("Gadget"),
            .properties = gadgetProperties,
        });
        workspaceClass = classes.registerClass({
            .name = atoms.intern("Workspace"),
            .defaultName = atoms.intern("Workspace"),
        });
        // The three the material preview is built out of. Registered here rather
        // than invented per case, because a preview that could not find `Part`
        // silently builds nothing -- which reads as "the feature is off" and is
        // exactly what a test must be able to tell apart.
        // **`Material` is declared here, with the class it may name**, because
        // the property is what the reference picker and the material drop are
        // about -- and a fixture `Part` that merely has a component but no
        // property would let both pass while the real class refused every write.
        partProperties = {
            scene::PropertyDesc{
                .name = atoms.intern("Material"),
                .type = scene::ValueType::Instance,
                .instanceClass = atoms.intern("Material"),
                .get = &getPartMaterial,
                .set = &setPartMaterial,
            },
        };
        partClass = classes.registerClass({
            .name = atoms.intern("Part"),
            .defaultName = atoms.intern("Part"),
            .properties = partProperties,
            .attachComponents = [](scene::World& w, core::InstanceId id) { w.parts().add(id, scene::PartComponent{}); },
            .detachComponents = [](scene::World& w, core::InstanceId id) { w.parts().remove(id); },
        });
        materialClass = classes.registerClass({
            .name = atoms.intern("Material"),
            .defaultName = atoms.intern("Material"),
            .attachComponents = [](scene::World& w,
                                   core::InstanceId id) { w.materials().add(id, scene::MaterialComponent{}); },
            .detachComponents = [](scene::World& w, core::InstanceId id) { w.materials().remove(id); },
        });
        // The world is watched from a camera when nothing else registers a
        // focus (D098), so a fixture that cannot make one cannot assert it.
        cameraClass = classes.registerClass({
            .name = atoms.intern("Camera"),
            .defaultName = atoms.intern("Camera"),
            .attachComponents = [](scene::World& w,
                                   core::InstanceId id) { w.cameras().add(id, scene::CameraComponent{}); },
            .detachComponents = [](scene::World& w, core::InstanceId id) { w.cameras().remove(id); },
        });
        pointLightClass = classes.registerClass({
            .name = atoms.intern("PointLight"),
            .defaultName = atoms.intern("PointLight"),
            .attachComponents = [](scene::World& w,
                                   core::InstanceId id) { w.pointLights().add(id, scene::PointLightComponent{}); },
            .detachComponents = [](scene::World& w, core::InstanceId id) { w.pointLights().remove(id); },
        });
        lightingClass = classes.registerClass({
            .name = atoms.intern("Lighting"),
            .defaultName = atoms.intern("Lighting"),
        });
        folderClass = classes.registerClass({
            .name = atoms.intern("Folder"),
            .defaultName = atoms.intern("Folder"),
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

    [[nodiscard]] core::InstanceId gadget(scene::World& world, std::string_view instanceName)
    {
        const core::InstanceId id = world.create(gadgetClass);
        world.setName(id, atoms.intern(instanceName));
        return id;
    }

private:
    std::vector<scene::EnumItemDesc> moodItems;
    std::vector<scene::PropertyDesc> thingProperties;
    std::vector<scene::PropertyDesc> widgetProperties;
    std::vector<scene::PropertyDesc> gadgetProperties;
    std::vector<scene::PropertyDesc> partProperties;
};

} // namespace luaug::app::testing
