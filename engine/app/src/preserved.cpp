#include "luaug/app/preserved.h"

#include <string_view>

namespace luaug::app
{
namespace
{

using scene::ClassId;
using scene::Value;

constexpr std::string_view kPreserveTag = "PreserveOnReload";

[[nodiscard]] std::string textOf(const scene::World& world, core::NameAtom atom)
{
    return std::string(world.atoms().text(atom));
}

[[nodiscard]] bool isService(const scene::World& world, core::InstanceId id)
{
    const scene::ClassDescriptor* descriptor = world.classes().find(world.classOf(id));
    return descriptor != nullptr && hasFlag(descriptor->flags, scene::ClassFlags::Service);
}

void captureInstance(
    const scene::World& world, core::InstanceId id, PreservedInstance& out, PreserveReport& report)
{
    out.className = textOf(world, world.classes().find(world.classOf(id))->name);
    out.name = textOf(world, world.name(id));

    const core::NameAtom parentProperty = world.atoms().lookup("Parent");

    for (const scene::ClassDescriptor* current = world.classes().find(world.classOf(id)); current != nullptr;
         current = world.classes().find(current->super))
    {
        for (const scene::PropertyDesc& property : current->properties)
        {
            if (property.readOnly || property.get == nullptr || property.set == nullptr)
                continue;
            // Structure rather than a value, and the restore is what sets it --
            // the same exclusion `World::clone` makes, for the same reason.
            if (property.name == parentProperty)
                continue;

            Value value = property.get(world, id);
            if (scene::valueType(value) == scene::ValueType::Instance)
            {
                // It points into the world being destroyed. Nothing in v1
                // declares one, so this is a counter rather than a policy: the
                // first class that does makes this number move.
                ++report.droppedReferences;
                continue;
            }

            out.properties.emplace_back(textOf(world, property.name), std::move(value));
        }
    }

    scene::AttributeMap attributes;
    world.collectAttributes(id, attributes);
    for (const auto& entry : attributes)
        out.attributes.emplace_back(textOf(world, entry.first), entry.second);

    scene::TagSet tags;
    world.collectTags(id, tags);
    for (const core::NameAtom tag : tags)
        out.tags.push_back(textOf(world, tag));

    std::vector<core::InstanceId> children;
    world.collectChildren(id, children);
    out.children.reserve(children.size());
    for (const core::InstanceId child : children)
    {
        PreservedInstance& record = out.children.emplace_back();
        captureInstance(world, child, record, report);
    }
}

// The chain from `game` down to `id`'s parent, outermost first. Empty and
// `false` when `id` does not hang under the DataModel at all.
[[nodiscard]] bool capturePath(
    const scene::World& world, core::InstanceId dataModel, core::InstanceId id, std::vector<PreservedAncestor>& out)
{
    std::vector<core::InstanceId> chain;
    for (core::InstanceId walk = world.parentOf(id); walk.valid(); walk = world.parentOf(walk))
    {
        if (walk == dataModel)
        {
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                out.push_back(PreservedAncestor{
                    textOf(world, world.classes().find(world.classOf(*it))->name),
                    textOf(world, world.name(*it)),
                    isService(world, *it),
                });
            }
            return true;
        }
        chain.push_back(walk);
    }
    return false;
}

[[nodiscard]] core::InstanceId resolveAncestor(
    scene::World& world, core::InstanceId parent, const PreservedAncestor& step)
{
    const ClassId classId = world.classes().findId(world.atoms().lookup(step.className));
    if (classId == scene::InvalidClass)
        return {};

    if (step.isService)
    {
        // Fetched or created by class, which is exactly what `GetService`
        // does -- a service is a singleton and asking for one is how it comes
        // to exist.
        if (const core::InstanceId existing = world.findFirstChildOfClass(parent, classId); existing.valid())
            return existing;
    }
    else if (const core::InstanceId existing = world.findFirstChild(parent, world.atoms().lookup(step.name));
             existing.valid())
    {
        return existing;
    }

    const core::InstanceId created = world.create(classId);
    if (!created.valid())
        return {};
    world.setName(created, world.atoms().intern(step.name));
    (void)world.setParent(created, parent);
    return created;
}

[[nodiscard]] core::InstanceId restoreInstance(
    scene::World& world, core::InstanceId parent, const PreservedInstance& record)
{
    const ClassId classId = world.classes().findId(world.atoms().lookup(record.className));
    if (classId == scene::InvalidClass)
        return {};

    const core::InstanceId created = world.create(classId);
    if (!created.valid())
        return {};

    world.setName(created, world.atoms().intern(record.name));

    for (const auto& entry : record.properties)
    {
        const scene::PropertyDesc* property = world.classes().findProperty(classId, world.atoms().intern(entry.first));
        // A property the new build no longer declares is dropped rather than
        // fatal: the engine changed under the world, which during a reload of a
        // freshly rebuilt engine is a thing that happens.
        if (property != nullptr && property->set != nullptr)
            (void)property->set(world, created, entry.second);
    }

    for (const auto& entry : record.attributes)
        (void)world.setAttribute(created, world.atoms().intern(entry.first), entry.second);

    for (const std::string& tag : record.tags)
        (void)world.addTag(created, world.atoms().intern(tag));

    // Parented after its own members are set and before its children exist, so
    // the child order below is the order they were captured in.
    (void)world.setParent(created, parent);

    for (const PreservedInstance& child : record.children)
        (void)restoreInstance(world, created, child);

    return created;
}

} // namespace

std::vector<PreservedTree> capturePreserved(
    const scene::World& world, core::InstanceId dataModel, PreserveReport& report)
{
    std::vector<PreservedTree> trees;

    const core::NameAtom tag = world.atoms().lookup(kPreserveTag);
    if (!tag.valid())
        return trees;

    std::vector<core::InstanceId> tagged;
    world.collectTagged(tag, tagged);

    const ClassId scriptServiceClass = world.classes().findId(world.atoms().lookup("ScriptService"));

    for (const core::InstanceId id : tagged)
    {
        // A tag on a descendant of an already-tagged instance is redundant: the
        // outer capture takes the whole subtree, and capturing it twice would
        // restore it twice.
        bool covered = false;
        for (core::InstanceId walk = world.parentOf(id); walk.valid(); walk = world.parentOf(walk))
        {
            if (world.hasTag(walk, tag))
            {
                covered = true;
                break;
            }
        }
        if (covered)
            continue;

        // Entry scripts are rebuilt from source by the mount, so preserving one
        // would put a second copy beside it.
        if (scriptServiceClass != scene::InvalidClass && world.findFirstAncestorOfClass(id, scriptServiceClass).valid())
        {
            ++report.skipped;
            continue;
        }

        PreservedTree tree;
        if (!capturePath(world, dataModel, id, tree.path))
        {
            // Nothing to put it back under. What is being preserved is where it
            // was as much as what it was.
            ++report.skipped;
            continue;
        }

        captureInstance(world, id, tree.root, report);
        trees.push_back(std::move(tree));
        ++report.captured;
    }

    return trees;
}

void restorePreserved(
    scene::World& world,
    core::InstanceId dataModel,
    const std::vector<PreservedTree>& trees,
    PreserveReport& report)
{
    for (const PreservedTree& tree : trees)
    {
        core::InstanceId parent = dataModel;
        for (const PreservedAncestor& step : tree.path)
        {
            parent = resolveAncestor(world, parent, step);
            if (!parent.valid())
                break;
        }

        if (!parent.valid() || !restoreInstance(world, parent, tree.root).valid())
        {
            ++report.skipped;
            continue;
        }
        ++report.restored;
    }

    // The tree was built before any script could connect to anything, so these
    // are facts with no observer -- the same reason `mountScripts` consumes its
    // own changes.
    (void)world.changes().take();
}

} // namespace luaug::app
