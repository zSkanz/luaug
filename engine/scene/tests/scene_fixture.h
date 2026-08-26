// A hand-built class hierarchy for the scene tests.
//
// The real descriptors are generated from `api/defs/*.api.luau`, and generating
// them is a later step; these mirror the shape that generator will emit --
// descriptor arrays, plain function-pointer accessors, component hooks per
// declaring class -- so that the tests exercise the same paths the generated
// tables will.
//
// The classes deliberately reuse the real components rather than inventing test
// ones. `World` exposes exactly the pools the M2 surface needs, and a test that
// invented a fourth would be testing a `World` nobody ships.
//
// The descriptor arrays are **per instance, not static**. A function-local
// static would be initialised from whichever `Hierarchy` happened to be built
// first, and every later one has its own `AtomTable` whose ids do not match --
// so every test after the first would look up properties that were named in
// somebody else's alphabet. Owning the storage also lets a test build the same
// world over two tables with different intern orders, which is the world hash's
// most important property.
#pragma once

#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <string>
#include <string_view>
#include <vector>

namespace luaug::scene::testing {

struct Hierarchy
{
    core::AtomTable atoms;
    ClassRegistry classes;
    // Deliberately empty. The fixture's `Shape` is a plain number rather than
    // an enum item, so nothing here validates against an enum -- and a fixture
    // that registered the real ones would be asserting the generator's output
    // from a file the generator does not write.
    EnumRegistry enums;

    ClassId instanceClass = InvalidClass;
    ClassId folderClass = InvalidClass;
    ClassId basePartClass = InvalidClass;
    ClassId partClass = InvalidClass;
    ClassId meshPartClass = InvalidClass;
    // `Attachment` and `Bone` share one component, so the fixture registers one
    // class and the bone half is a `JointName` written into it -- which is what
    // the real hierarchy does too.
    ClassId attachmentClass = InvalidClass;
    // Welds were only ever exercised through the conformance specs. The mirror
    // now has behaviour worth asserting in C++ -- an attachment as an anchor,
    // and the resolution order that keeps it from lagging a frame.
    ClassId weldClass = InvalidClass;
    // One class stands in for the three concrete constraints: they differ only
    // in the `kind` their own hook stamps, and a test sets that directly.
    ClassId constraintClass = InvalidClass;
    ClassId ragdollClass = InvalidClass;
    ClassId modelClass = InvalidClass;

    core::NameAtom nameProperty;
    core::NameAtom transparencyProperty;
    core::NameAtom sizeProperty;
    core::NameAtom cframeProperty;
    core::NameAtom shapeProperty;
    core::NameAtom primaryPartProperty;
    // **What a ragdoll build writes**, and the reason they are here: everything
    // that touched an `Attachment` or a `Constraint` before wrote the component
    // directly, so a caller going through `setProperty` -- which is what a build
    // action and a scene load both do -- found no descriptor and silently wrote
    // nothing. A fixture that mirrors the real hierarchy has to declare them.
    core::NameAtom jointNameProperty;
    core::NameAtom attachment0Property;
    core::NameAtom attachment1Property;
    core::NameAtom collideConnectedProperty;

    Hierarchy();

    // Non-copyable and non-movable: the registry holds spans into the vectors
    // below, so relocating one would leave every descriptor pointing at freed
    // storage.
    Hierarchy(const Hierarchy&) = delete;
    Hierarchy& operator=(const Hierarchy&) = delete;

private:
    std::vector<PropertyDesc> m_instanceProperties;
    std::vector<PropertyDesc> m_basePartProperties;
    std::vector<PropertyDesc> m_partProperties;
    std::vector<PropertyDesc> m_modelProperties;
    std::vector<PropertyDesc> m_attachmentProperties;
    std::vector<PropertyDesc> m_constraintProperties;
};

// A world over that hierarchy, with the seed fixed so every test is a replay of
// the same stream.
struct Fixture
{
    Hierarchy schema;
    World world{schema.classes, schema.enums, schema.atoms, 1234u};

    [[nodiscard]] core::NameAtom atom(std::string_view text) { return schema.atoms.intern(text); }

    [[nodiscard]] core::InstanceId folder(std::string_view instanceName)
    {
        const core::InstanceId id = world.create(schema.folderClass);
        world.setName(id, atom(instanceName));
        return id;
    }

    [[nodiscard]] core::InstanceId part(std::string_view instanceName)
    {
        const core::InstanceId id = world.create(schema.partClass);
        world.setName(id, atom(instanceName));
        return id;
    }

    [[nodiscard]] core::InstanceId model(std::string_view instanceName)
    {
        const core::InstanceId id = world.create(schema.modelClass);
        world.setName(id, atom(instanceName));
        return id;
    }

    // Read back as text, which is what an assertion should compare: atom
    // numbers depend on intern order and would make a failure unreadable.
    [[nodiscard]] std::string nameOf(core::InstanceId id) const
    {
        return std::string(schema.atoms.text(world.name(id)));
    }

    [[nodiscard]] std::vector<std::string> childNames(core::InstanceId id) const
    {
        std::vector<core::InstanceId> children;
        world.collectChildren(id, children);
        std::vector<std::string> out;
        for (const core::InstanceId child : children)
            out.push_back(nameOf(child));
        return out;
    }

    [[nodiscard]] std::vector<std::string> descendantNames(core::InstanceId id) const
    {
        std::vector<core::InstanceId> descendants;
        world.collectDescendants(id, descendants);
        std::vector<std::string> out;
        for (const core::InstanceId descendant : descendants)
            out.push_back(nameOf(descendant));
        return out;
    }
};

} // namespace luaug::scene::testing
