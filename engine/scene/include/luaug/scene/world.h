// The scene: an ECS underneath, an Instance tree on top (architecture.md §4,
// ADR 0026, ADR 0028).
//
// `World` owns entity lifetime, the hierarchy, names, attributes, tags and the
// change queue. It holds no `lua_State` and includes no Luau header; the
// bindings that turn a `Value` into a Luau value live in `script` (L5), and the
// facts this produces mean something in a headless world with no VM at all.
//
// Tree mutation is **synchronous** and its notifications are **deferred**
// (api-design.md §3.1). `destroy` removes the subtree before it returns; what
// waits for the next resumption point is the telling, not the doing. Getting
// that backwards would make `part.Parent = nil` followed by a `GetChildren`
// read inconsistent, which is the kind of thing scripts are written against
// without anyone thinking about it.
#pragma once

#include <optional>
#include <string>
#include <span>
#include <unordered_map>
#include <vector>

#include "luaug/core/id.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/random.h"
#include "luaug/core/slotmap.h"
#include "luaug/core/text_key.h"
#include "luaug/scene/change_queue.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/component_pool.h"
#include "luaug/scene/components.h"
#include "luaug/scene/types.h"
#include "luaug/scene/value.h"

namespace luaug::scene
{

// Intrusive links plus the duplicate-name chain (ADR 0026). `lastChild` exists
// so that appending is O(1) -- child order is parenting order, and every parent
// operation appends.
struct InstanceRecord
{
    ClassId classId = InvalidClass;
    core::NameAtom name;

    core::InstanceId parent;
    core::InstanceId firstChild;
    core::InstanceId lastChild;
    core::InstanceId prevSibling;
    core::InstanceId nextSibling;
    u32 childCount = 0;

    // The next sibling sharing this instance's name, in child order. Eight
    // bytes per instance to make `FindFirstChild` O(1) on a parent with three
    // children called "Tree" -- the case a plain name map would silently
    // clobber (ADR 0026).
    core::InstanceId nextSameName;

    // Set by `destroy` and never cleared. The handle still resolves until the
    // end of the drain that carries its `Destroying`, and during that window
    // `Parent` is locked (api-design.md divergence #25).
    bool destroyed = false;

    // Which of the class's first 64 properties have a listener. A write to an
    // unsubscribed property enqueues nothing, which is what lets 10k parts move
    // every tick for free while nobody is watching (architecture.md §4). Past
    // 64 properties the mask saturates and every write enqueues -- correct, and
    // slower, for a class nothing in v1 has.
    u64 subscribedProperties = 0;
};

// Insertion-ordered rather than hashed: `GetAttributes` and the world hash both
// walk this, and R10 forbids either observing a container's own order.
using AttributeMap = std::vector<std::pair<core::NameAtom, Value>>;
using TagSet = std::vector<core::NameAtom>;

// Engine state that a *service* property reads.
//
// `RunService.SimTime` and `PhysicsService.FixedTimestep` are produced by the
// frame scheduler, which lives in `app` at L6 -- and the descriptors that
// expose them are scene's, at L3, which cannot see it. So the scheduler writes
// here and the accessors read here. One instance per world rather than a
// component per service, because there is exactly one of each service in a
// world and a component would be ceremony around a single struct.
struct EngineState
{
    // Seconds, constant for the whole tick and advanced by the scheduler
    // (api-design.md §2.1).
    f64 simTime = 0.0;
    f64 fixedTimestep = 1.0 / 60.0;
    bool paused = false;
    bool overlayVisible = false;
    std::string engineVersion;
    std::string luauVersion;
};

// Per-parent name index. Held in its own pool rather than inline in the record
// so that a leaf instance -- most of them -- pays nothing for it.
struct NameIndex
{
    std::unordered_map<u32, core::InstanceId> firstByName;
};

class World
{
public:
    World(ClassRegistry& classes, core::AtomTable& atoms, u64 seed);

    // --- Lifetime ------------------------------------------------------------

    // Fails (returns an invalid id) for an unknown, abstract or non-creatable
    // class; the caller raises the key. `Name` starts at the class's
    // `defaultName` and the instance starts unparented.
    [[nodiscard]] core::InstanceId create(ClassId classId);

    // Synchronous: the subtree is out of the tree when this returns. Enqueues
    // `Destroying` for the instance and each descendant, clears their tags, and
    // locks `Parent`. The generation bump -- which is what stops the handle
    // resolving -- happens in `retireDestroyed`, called at the end of the drain.
    bool destroy(core::InstanceId id);

    // Called by the scheduler after a drain completes. Splitting this from
    // `destroy` is what gives `Destroying` handlers a live handle to work with,
    // which is the whole reason the signal exists.
    void retireDestroyed();

    [[nodiscard]] bool alive(core::InstanceId id) const noexcept;
    [[nodiscard]] ClassId classOf(core::InstanceId id) const noexcept;
    [[nodiscard]] bool isA(core::InstanceId id, ClassId base) const noexcept;

    // --- Naming --------------------------------------------------------------

    [[nodiscard]] core::NameAtom name(core::InstanceId id) const noexcept;

    // Relinks the parent's name chains, so a rename can satisfy a
    // `WaitForChild` that was parked on the new name (api-design.md §2.2).
    void setName(core::InstanceId id, core::NameAtom newName);

    // --- Hierarchy -----------------------------------------------------------

    [[nodiscard]] core::InstanceId parentOf(core::InstanceId id) const noexcept;
    [[nodiscard]] core::InstanceId firstChild(core::InstanceId id) const noexcept;
    [[nodiscard]] core::InstanceId nextSibling(core::InstanceId id) const noexcept;
    [[nodiscard]] u32 childCount(core::InstanceId id) const noexcept;

    // Returns the error key on refusal and leaves the tree untouched:
    // `scene.err.parent_cycle` for a cycle, `scene.err.parent_locked` for a
    // destroyed instance. Re-assigning the current parent is a no-op and does
    // NOT reorder the child (api-design.md §2.2).
    std::optional<core::TextKey> setParent(core::InstanceId id, core::InstanceId newParent);

    // O(1) and first in child order, duplicate names included (ADR 0026).
    [[nodiscard]] core::InstanceId findFirstChild(core::InstanceId parent, core::NameAtom childName) const noexcept;
    // Exact `ClassName` match, so asking for `BasePart` never finds a `Part`.
    [[nodiscard]] core::InstanceId findFirstChildOfClass(core::InstanceId parent, ClassId classId) const noexcept;
    // Matches through the hierarchy, so an abstract base name is accepted.
    [[nodiscard]] core::InstanceId findFirstChildWhichIsA(core::InstanceId parent, ClassId base) const noexcept;
    [[nodiscard]] core::InstanceId findFirstAncestor(core::InstanceId id, core::NameAtom ancestorName) const noexcept;
    [[nodiscard]] core::InstanceId findFirstAncestorOfClass(core::InstanceId id, ClassId classId) const noexcept;

    // Strict: an instance is neither its own ancestor nor its own descendant.
    [[nodiscard]] bool isAncestorOf(core::InstanceId id, core::InstanceId descendant) const noexcept;

    // Appends children in child order. Fresh vectors are the caller's, which is
    // what makes destroying during iteration safe (api-design.md §3.1).
    void collectChildren(core::InstanceId id, std::vector<core::InstanceId>& out) const;
    // Depth-first preorder -- the same document order the Find family
    // tie-breaks on.
    void collectDescendants(core::InstanceId id, std::vector<core::InstanceId>& out) const;

    // Deep copy, unparented. References that point inside the copied subtree
    // are rewired to the copies; references that point outside it are left on
    // the originals (api-design.md §2.6).
    [[nodiscard]] core::InstanceId clone(core::InstanceId id);

    // --- Properties ----------------------------------------------------------

    // Nullopt for a name the class does not have; the caller raises
    // `scene.err.unknown_member`.
    [[nodiscard]] std::optional<Value> getProperty(core::InstanceId id, core::NameAtom property) const;

    // The result distinguishes the three ways a write can fail, because each
    // raises a different key.
    enum class SetResult : u8
    {
        Changed,
        // Written, but equal to what was already there, so nothing was
        // enqueued (api-design.md §3.1).
        Unchanged,
        UnknownProperty,
        ReadOnly,
        InvalidValue,
    };
    SetResult setProperty(core::InstanceId id, core::NameAtom property, const Value& value);

    // `script` maintains this as connections come and go. It is the switch that
    // decides whether a property write is quiet.
    void setPropertySubscribed(core::InstanceId id, core::NameAtom property, bool subscribed);

    // --- Attributes and tags -------------------------------------------------

    [[nodiscard]] Value getAttribute(core::InstanceId id, core::NameAtom attribute) const;
    // A `Nil` value removes the attribute. Rejects a value outside the
    // documented domain, which the caller reports as `scene.err.attribute_type`.
    bool setAttribute(core::InstanceId id, core::NameAtom attribute, const Value& value);
    void collectAttributes(core::InstanceId id, AttributeMap& out) const;

    bool addTag(core::InstanceId id, core::NameAtom tag);
    bool removeTag(core::InstanceId id, core::NameAtom tag);
    [[nodiscard]] bool hasTag(core::InstanceId id, core::NameAtom tag) const noexcept;
    void collectTags(core::InstanceId id, TagSet& out) const;
    // Tagging is independent of the tree, so a nil-parented instance is listed.
    void collectTagged(core::NameAtom tag, std::vector<core::InstanceId>& out) const;
    void collectAllTags(TagSet& out) const;

    // --- Frame plumbing ------------------------------------------------------

    [[nodiscard]] ChangeQueue& changes() noexcept { return m_changes; }

    // xxh3 over a canonical walk of the simulation-relevant state
    // (architecture.md §9). Never touches an atom's numeric value, which
    // depends on intern order; it hashes the text.
    [[nodiscard]] u64 worldHash() const;

    // The world's own deterministic stream. `Random.new()` without a seed does
    // NOT come from here and is not legal in simulation code (api-design.md
    // §2.3).
    [[nodiscard]] core::Pcg32& rng() noexcept { return m_rng; }

    [[nodiscard]] usize instanceCount() const noexcept { return m_instances.size(); }
    [[nodiscard]] EngineState& engineState() noexcept { return m_engineState; }
    [[nodiscard]] const EngineState& engineState() const noexcept { return m_engineState; }

    [[nodiscard]] core::AtomTable& atoms() noexcept { return m_atoms; }
    // A property getter takes a `const World&`, and resolving an atom to text
    // is the one thing it routinely needs the table for.
    [[nodiscard]] const core::AtomTable& atoms() const noexcept { return m_atoms; }
    [[nodiscard]] const ClassRegistry& classes() const noexcept { return m_classes; }

    // Component storage the generated property accessors read and write. Public
    // because those accessors are free functions in generated code rather than
    // members -- the alternative is a friend declaration per class, generated.
    //
    // Named per component rather than a `pool<T>()` template, because every
    // class in the M2 surface is scene's own. Architecture §2 rule 3 has higher
    // modules register their components into scene, and the type-erased storage
    // that needs is a problem to solve when M4 brings the first one, not to
    // guess at now.
    [[nodiscard]] ComponentPool<PartComponent>& parts() noexcept { return m_parts; }
    [[nodiscard]] const ComponentPool<PartComponent>& parts() const noexcept { return m_parts; }
    [[nodiscard]] ComponentPool<ModelComponent>& models() noexcept { return m_models; }
    [[nodiscard]] const ComponentPool<ModelComponent>& models() const noexcept { return m_models; }
    [[nodiscard]] ComponentPool<ScriptComponent>& scripts() noexcept { return m_scripts; }
    [[nodiscard]] const ComponentPool<ScriptComponent>& scripts() const noexcept { return m_scripts; }

private:
    void linkChild(InstanceRecord& parentRecord, core::InstanceId parentId, core::InstanceId childId);
    void unlinkChild(core::InstanceId childId);
    void indexName(core::InstanceId parentId, core::InstanceId childId);
    void unindexName(core::InstanceId parentId, core::InstanceId childId);

    ClassRegistry& m_classes;
    core::AtomTable& m_atoms;
    core::Pcg32 m_rng;

    core::SlotMap<InstanceRecord> m_instances;
    ComponentPool<PartComponent> m_parts;
    ComponentPool<ModelComponent> m_models;
    ComponentPool<ScriptComponent> m_scripts;
    ComponentPool<NameIndex> m_nameIndices;
    ComponentPool<AttributeMap> m_attributes;
    ComponentPool<TagSet> m_tags;
    // Insertion-ordered per tag, so `GetTagged` never leaks a hash order.
    std::unordered_map<u32, std::vector<core::InstanceId>> m_tagged;

    // Destroyed but not yet retired: their handles still resolve until the
    // drain that carries their `Destroying` finishes.
    std::vector<core::InstanceId> m_pendingRetire;

    EngineState m_engineState;
    ChangeQueue m_changes;
};

} // namespace luaug::scene
