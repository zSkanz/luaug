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

#include "luaug/core/id.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/random.h"
#include "luaug/core/slotmap.h"
#include "luaug/core/text_key.h"
#include "luaug/scene/change_queue.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/collision_groups.h"
#include "luaug/scene/component_pool.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/types.h"
#include "luaug/scene/value.h"

#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace luaug::scene {

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

    // **Created by a system rather than authored by a person**, and therefore
    // not part of a scene.
    //
    // A streamed chunk is the case that forced this: `StreamingGlue` puts a
    // `Folder` of real `Part`s into `Workspace` as a focus moves, and a save
    // taken at that moment wrote a recording of where the streaming happened to
    // be -- 40 chunks and 1.4 MB of terrain that already has a format of its
    // own, frozen into a file that is supposed to describe a project. Unity
    // spells the same idea `HideFlags.DontSave`.
    //
    // It is deliberately NOT in the world hash. The hash asks what the
    // simulation is, and a streamed part is as real to a tick as an authored
    // one; this asks what a person wrote down, which is a different question
    // with a different answer.
    bool generated = false;

    // **The STAMP this instance was made from**, or an empty atom (ADR 0049).
    //
    // The same kind of fact as `generated` above and stored beside it for the
    // same reasons: it is what a PERSON wrote down rather than what the
    // simulation is, it travels in a snapshot so undo and stop keep it, and it
    // is deliberately not in the world hash -- a stamped part ticks like any
    // other, and two worlds that differ only in where their parts came from are
    // the same world to a solver.
    //
    // An atom rather than a string because it is a path repeated across every
    // instance of one stamp: forty lamp posts intern one name.
    core::NameAtom stamp;

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
    // The tick index `simTime` corresponds to. Held as an integer because every
    // `task` deadline is computed in whole ticks and never by comparing floats:
    // at dt = 1/60, `1 / (1/60)` is 60.000000000000007, and a deadline derived
    // from that float is a `task.wait(1)` that lasts 61 ticks (api-design.md
    // §3.2).
    u64 tick = 0;
    // The tick the scheduler is running on. Changed only at a FrameStart safe
    // point, never mid-frame: the accumulator, the timer wheel and the solver
    // all read it, and a value that changed between two of those reads inside
    // one frame is a class of bug worth designing out (api-design.md §2.1).
    f64 fixedTimestep = 1.0 / 60.0;
    // What `PhysicsService.FixedTimestep` was last written to, and what reading
    // it gives back. The scheduler copies this into `fixedTimestep` at the next
    // safe point, so a write round-trips immediately and takes effect one frame
    // later -- which is what the property's Doc promises.
    f64 requestedFixedTimestep = 1.0 / 60.0;
    bool paused = false;
    bool overlayVisible = false;

    // `StreamingService`'s knobs (M7). Here rather than in a component for the
    // same reason `FixedTimestep` is: a service with one instance and no
    // hierarchy of its own has nothing a component would buy, and the host
    // reads these once per frame to build the manager's focus list.
    //
    // The radii are in studs and the pair is a RANGE rather than a value: the
    // engine keeps chunks inside `streamingLoadRadius` and guarantees the ones
    // inside `streamingMinRadius` before a focus may advance into them.
    bool streamingEnabled = true;
    f64 streamingLoadRadius = 1024.0;
    f64 streamingMinRadius = 512.0;
    bool streamingPauseOutsideLoadedArea = false;

    // The other two size classes' radii (ADR 0053). A cell's `layer` is its
    // class -- 0 detail, 1 structures, 2 terrain features -- and the pair above
    // is layer 0's, which is why a world whose cells are all layer 0 needs
    // nothing here and behaves exactly as it did.
    //
    // **Zero means "follow the pair above"**, rather than "a radius of zero".
    // The alternative is defaulting them to 512 and 1024 as well, and then a
    // person who raises `LoadRadius` alone finds their terrain no further away
    // than before -- a knob that silently stops applying is worse than one that
    // has to be read about.
    f64 streamingStructureMinRadius = 0.0;
    f64 streamingStructureLoadRadius = 0.0;
    f64 streamingTerrainMinRadius = 0.0;
    f64 streamingTerrainLoadRadius = 0.0;

    // `InputService`'s own state (M6). The pointer position is a SNAPSHOT taken
    // once per frame, like M5's keyboard: two reads inside one tick agree, and a
    // recorded input stream can hand the same answer back with no mouse.
    core::Vec2 pointerPosition;
    bool pointerLocked = false;
    bool pointerVisible = true;
    // `Enum.InputDeviceType`: 0 KeyboardMouse, 1 Gamepad, 2 Touch.
    i32 lastInputDeviceType = 0;

    // `UIService`'s two read-only numbers (M6). Both are properties of the
    // WINDOW, written by the host each frame, and both are zero-ish on a
    // desktop -- which is exactly why a HUD that ignores the insets looks fine
    // until somebody runs it on a phone.
    core::Rect safeAreaInsets;
    f32 displayScale = 1.0f;

    // `AudioService.MasterVolume` (M6). Here for the same reason
    // `PhysicsService.FixedTimestep` is: one of each service per world, and a
    // component around a single float would be ceremony.
    f32 masterVolume = 1.0f;
    std::string engineVersion;
    std::string luauVersion;
};

// Per-parent name index. Held in its own pool rather than inline in the record
// so that a leaf instance -- most of them -- pays nothing for it.
struct NameIndex
{
    std::unordered_map<u32, core::InstanceId> firstByName;
};

// Every component pool a `World` owns, named ONCE.
//
// The world's members, a snapshot's storage and the copy between them are all
// generated from this list, so a pool that is not here does not exist and a
// pool that is here but nowhere else fails to compile. The alternative is three
// hand-kept lists that agree until the day somebody adds a component and
// updates two of them -- and the symptom of that is a component that quietly
// stops surviving a restore, which no test asks about unless somebody thought
// to write one.
#define LUAUG_SCENE_POOL_LIST(X)                                                                                       \
    X(PVComponent, pvInstances)                                                                                        \
    X(PartComponent, parts)                                                                                            \
    X(RigidBodyComponent, rigidBodies)                                                                                 \
    X(CharacterBodyComponent, characterBodies)                                                                         \
    X(WeldComponent, welds)                                                                                            \
    X(AttachmentComponent, attachments)                                                                                \
    X(ConstraintComponent, constraints)                                                                                \
    X(RagdollComponent, ragdolls)                                                                                      \
    X(MaterialComponent, materials)                                                                                    \
    X(WorkspaceComponent, workspaces)                                                                                  \
    X(ModelComponent, models)                                                                                          \
    X(ScriptComponent, scripts)                                                                                        \
    X(SoundComponent, sounds)                                                                                          \
    X(AudioGroupComponent, audioGroups)                                                                                \
    X(ScreenGuiComponent, screenGuis)                                                                                  \
    X(UIObjectComponent, uiObjects)                                                                                    \
    X(TextLabelComponent, textLabels)                                                                                  \
    X(TextInputComponent, textInputs)                                                                                  \
    X(ImageLabelComponent, imageLabels)                                                                                \
    X(ScrollFrameComponent, scrollFrames)                                                                              \
    X(UIListLayoutComponent, listLayouts)                                                                              \
    X(UIPaddingComponent, uiPaddings)                                                                                  \
    X(UICornerComponent, uiCorners)                                                                                    \
    X(InputContextComponent, inputContexts)                                                                            \
    X(InputActionComponent, inputActions)                                                                              \
    X(InputBindingComponent, inputBindings)                                                                            \
    X(MeshPartComponent, meshParts)                                                                                    \
    X(CameraComponent, cameras)                                                                                        \
    X(PointLightComponent, pointLights)                                                                                \
    X(SpotLightComponent, spotLights)                                                                                  \
    X(LightingComponent, lighting)                                                                                     \
    X(NameIndex, nameIndices)                                                                                          \
    X(AttributeMap, attributes)                                                                                        \
    X(TagSet, tags)

// A whole world's state, held in memory (ADR 0016's "snapshottable POD ECS
// pools" -- foundations, not rollback).
//
// What it is for is the editor's Play and Stop: the world is copied when Play
// is pressed and put back when Stop is, so a play session leaves nothing
// behind. It is **not** a file format. Nothing here is versioned, nothing is
// byte-stable across builds, and a snapshot means something only to the `World`
// it came from, with the same `ClassRegistry`, `EnumRegistry` and `AtomTable`
// still alive behind it. Serialization is a separate thing with separate
// problems.
//
// Copying rather than journalling is the whole design, and it is why the
// module was shaped the way it was: every component is POD with no invariants
// of its own (components.h), every pool is a dense array whose removals leave
// holes rather than reordering (component_pool.h), the hierarchy is intrusive
// links inside the records, and an enum is `{enumId, value}` rather than a
// pointer (value.h, enum_registry.h). So a snapshot is a per-pool copy and
// never a traversal, and a restore is the same copy the other way.
struct WorldSnapshot
{
#define LUAUG_SCENE_POOL_SNAPSHOT_MEMBER(Type, Name) ComponentPool<Type> Name;
    LUAUG_SCENE_POOL_LIST(LUAUG_SCENE_POOL_SNAPSHOT_MEMBER)
#undef LUAUG_SCENE_POOL_SNAPSHOT_MEMBER

    // Generations and the free list included, which is what makes an
    // `InstanceId` mean the same thing after a restore as before it.
    core::SlotMap<InstanceRecord> instances;
    std::unordered_map<u32, std::vector<core::InstanceId>> tagged;
    std::vector<core::InstanceId> pendingRetire;
    std::vector<core::InstanceId> streamingFoci;
    // `CollisionGroups` has no default constructor because a world's table
    // always has a `Default` group; a snapshot's is overwritten before anyone
    // reads it, so the atom it is seeded with is never observed.
    CollisionGroups collisionGroups{core::NameAtom{}};
    EngineState engineState;
    // The generator's state rather than the generator, so restoring it is the
    // same `setState` a `Random:Clone` performs (core/random.h).
    u64 rngState = 0;
    u64 rngIncrement = 1;
};

class World
{
public:
    World(ClassRegistry& classes, EnumRegistry& enums, core::AtomTable& atoms, u64 seed);

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

    // Whether `destroy` has been called and the retirement has not happened
    // yet. `alive` is deliberately true through that window so a `Destroying`
    // handler has a handle to work with, so the two questions are different and
    // a caller that means "is this still part of the world" wants this one.
    //
    // The renderer is the first to need it: a camera destroyed mid-drain is
    // still `alive`, and rendering through it would be drawing a frame from a
    // viewpoint the world has already let go of.
    [[nodiscard]] bool destroyed(core::InstanceId id) const noexcept;
    [[nodiscard]] ClassId classOf(core::InstanceId id) const noexcept;
    [[nodiscard]] bool isA(core::InstanceId id, ClassId base) const noexcept;

    // --- Naming --------------------------------------------------------------

    [[nodiscard]] core::NameAtom name(core::InstanceId id) const noexcept;

    // Marks an instance as made by a system rather than authored, so a scene
    // does not record it. Applies to the whole subtree at write time -- marking
    // a chunk's folder is enough, and marking every part inside it would be the
    // same statement a thousand times.
    void setGenerated(core::InstanceId id, bool generated) noexcept;
    [[nodiscard]] bool generated(core::InstanceId id) const noexcept;

    // The stamp this instance was made from, or an empty atom (ADR 0049).
    //
    // **Only the root of a stamped subtree carries it.** Its children are the
    // stamp's contents, not instances of it, and marking each of them would be
    // the same statement a hundred times -- the economy `generated` already
    // uses for a streamed chunk. `stampRootOf` is how a caller asks "am I
    // inside one", because that is the question the break rule actually needs.
    void setStamp(core::InstanceId id, core::NameAtom stamp) noexcept;
    [[nodiscard]] core::NameAtom stampOf(core::InstanceId id) const noexcept;

    // The nearest ancestor-or-self that carries a stamp, or an invalid id.
    //
    // The break rule is "everything inside a stamped subtree except the root's
    // own transform and name", so every caller that asks about an edit has to
    // walk up. Answering it here rather than in the editor keeps one definition
    // of "inside a stamp" for the editor, the serializer and the panel.
    [[nodiscard]] core::InstanceId stampRootOf(core::InstanceId id) const noexcept;

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

    // Where `id` sits among its parent's children, counting from zero. Nullopt
    // when it has no parent and when the handle resolves to nothing -- an
    // unparented instance has no siblings, and answering "first" would be
    // indistinguishable from a real position.
    //
    // Beside `moveChild` because a caller that has to decide whether a move
    // would change anything BEFORE it commits to an undo step needs the current
    // position, and an editor that walked the list itself would be a second
    // definition of "position among siblings".
    [[nodiscard]] std::optional<u32> siblingIndex(core::InstanceId id) const noexcept;

    // What `moveChild` did.
    //
    // An enum rather than an error key, for the reason `SetResult` below is
    // one: the two refusals want different messages, and a move that changes
    // nothing is neither a refusal nor a change -- which `std::optional<TextKey>`
    // has no way to say.
    enum class MoveResult : u8
    {
        Moved,
        // Already at that index. Nothing was touched and nothing was enqueued:
        // a step that changes nothing is a step that eats an undo, and
        // recording one clears the redo stack with it (D134).
        Unchanged,
        // `child` is not a child of `parent` -- it belongs to something else,
        // it has no parent at all, or one of the two handles no longer
        // resolves. One answer for three, because each is the same statement
        // about the same list; a caller that needs them apart has `parentOf`.
        //
        // A destroyed instance arrives here rather than at a `parent_locked` of
        // its own: `destroy` unparents the whole subtree before it marks it, so
        // there is no list left to hold a place in.
        NotAChild,
        // `index` is not a place in that child list. Refused rather than
        // clamped: the index is computed from where a person let go of a row,
        // and a clamp would put the instance somewhere else and report success.
        IndexOutOfRange,
    };

    // Moves an existing child to `index` among its parent's children, counting
    // from zero, where `index` is the place it will OCCUPY -- the child comes
    // out of the list and goes back in at that position, which is how Unity's
    // `SetSiblingIndex` and Godot's `move_child` both read. Reading it as
    // "before whatever stands at `index` now" would land a downward drag one
    // place short, every time.
    //
    // The one hierarchy verb that is not a re-parent. `setParent` appends and
    // deliberately does not reorder (api-design.md §2.2), which is why dropping
    // a row BETWEEN two rows in the editor's Explorer had nothing to call. It
    // is engine-side: the script-facing API has no reorder and no signal for
    // one, and a scene file is the only thing that records the result.
    //
    // **It enqueues nothing, and that is a decision rather than an omission.**
    // `ChangeKind` has no reorder, and the six signals api-design.md §2.2 lists
    // contain none this could feed -- so the only entries available would be a
    // `ChildRemoved` and a `ChildAdded` for a child that never left its parent,
    // which is two false statements to any handler that looks at the tree
    // during the drain. The world hash sees the move regardless, because it
    // walks the child list in sibling order; so does the serializer, which
    // makes the same walk. That is what keeps a rearranged scene saving as what
    // is on screen.
    //
    // **Cost is O(the parent's children), and it is the editor that pays it.**
    // The links are per record, so the relink itself is a handful of stores;
    // what is linear is finding the child's own position, finding the instance
    // then standing at `index`, and putting the child back in the right place
    // in its duplicate-name chain. A parent with a thousand children costs
    // three walks of a thousand, once per drop, on no tick path. The
    // alternative is a position stored per record, which every append would
    // then have to maintain -- and the 10k-parts benchmark would pay for that
    // on every parenting, to make a gesture nobody performs in a loop faster.
    MoveResult moveChild(core::InstanceId parent, core::InstanceId child, u32 index);

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

    // --- Snapshot and restore ------------------------------------------------

    // Everything `worldHash` calls observable -- the instance records, the
    // hierarchy including sibling order, names, attributes, tags and every
    // component pool -- plus the state the hash does not reach: `EngineState`,
    // the collision-group table, the streaming foci and the world's RNG
    // position.
    [[nodiscard]] WorldSnapshot snapshot() const;

    // Puts this world back. Instances created since the snapshot are gone,
    // instances destroyed since it are back with their components, and an
    // `InstanceId` taken BEFORE the snapshot resolves to the same instance
    // afterwards -- which is what lets an editor hold a selection across a Stop.
    // An id handed out DURING the play session resolves to nothing, and is
    // never handed out again to mean something else (`SlotMap::restoreFrom`
    // explains the generation bookkeeping that costs).
    //
    // Call it at a frame boundary with no drain in flight, the same safe point
    // `ComponentPool::compact` asks for. The change queue is CLEARED rather
    // than restored: its entries are facts about a world that no longer exists,
    // and their only consumer is the VM the caller is about to rebuild.
    //
    // **What a restore cannot put back, and what the caller therefore owes.**
    //
    //   * **The Luau VM.** Script variables, connections, coroutines and the
    //     task scheduler's timers are not this module's state and are not in
    //     the snapshot -- ADR 0016 names Luau-state restoration an explicit
    //     non-goal of v1, and `change_queue.h` explains why `scene` holds no
    //     reference into the VM to restore in the first place. The caller
    //     rebuilds the runtime. `InstanceRecord::subscribedProperties` comes
    //     back as it was at snapshot time and the rebuilt VM re-subscribes what
    //     it actually connects; a bit left set for a connection that no longer
    //     exists costs an enqueue nobody consumes, which is why the mask is
    //     restored rather than cleared.
    //   * **The physics mirror.** `PhysicsSync` keeps a body per instance slot
    //     and a character per id, and the backend behind it holds contacts,
    //     velocities and sleep state that `IPhysics3D::restoreState` does not
    //     implement (the Jolt backend answers false). A restored world's
    //     instances therefore correspond to nothing the solver holds: the
    //     caller destroys the mirror and builds a new one over the restored
    //     tree.
    //   * **Everything else derived and keyed by instance.**
    //     `render::AnimationSystem` (a track per player, a pose per mesh part),
    //     `render::TransformHistory` (last frame's transform, which motion
    //     vectors read), the audio mixer's voices and the UI's layout cache are
    //     all rebuilt from the tree rather than restored. Each already retires
    //     what stops resolving; what a restore adds is instances that REAPPEAR,
    //     which nothing retires. Safe order: restore, then rebuild.
    //   * **The atom table.** Names interned during the play session stay
    //     interned, because the table is shared and append-only. An atom's
    //     number is never observable -- the world hash hashes text for exactly
    //     this reason -- so a table that grew is a few bytes and not a
    //     difference.
    void restore(const WorldSnapshot& snapshot);

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

    // The collision-group table (api-design.md §2.1). World state rather than
    // the physics backend's, because a script writes it, reads it back and
    // replays it -- and because `BasePart.CollisionGroup` is validated against
    // it on every write, which a scene-level accessor cannot do if the table
    // lives below the seam.
    [[nodiscard]] CollisionGroups& collisionGroups() noexcept { return m_collisionGroups; }
    [[nodiscard]] const CollisionGroups& collisionGroups() const noexcept { return m_collisionGroups; }

    // `StreamingService`'s focus set (M7). A sorted vector rather than a set:
    // it holds two or three entries, the host walks it every frame, and R10
    // forbids an unordered container's order reaching observable output -- the
    // order foci are scored in decides which chunk wins a tie.
    [[nodiscard]] std::vector<core::InstanceId>& streamingFoci() noexcept { return m_streamingFoci; }
    [[nodiscard]] const std::vector<core::InstanceId>& streamingFoci() const noexcept { return m_streamingFoci; }

    [[nodiscard]] core::AtomTable& atoms() noexcept { return m_atoms; }
    // A property getter takes a `const World&`, and resolving an atom to text
    // is the one thing it routinely needs the table for.
    [[nodiscard]] const core::AtomTable& atoms() const noexcept { return m_atoms; }

private:
    std::vector<core::InstanceId> m_streamingFoci;

public:
    [[nodiscard]] const ClassRegistry& classes() const noexcept { return m_classes; }
    // Non-const, so a caller holding one world can build a SECOND against the
    // same registries -- which the prefab stage does, and which the serializer
    // does to diff a stamped instance against the stamp it came from. A
    // registry is a build-time fact shared by every world in a process; two
    // copies of one would be two answers to "what is a Part".
    [[nodiscard]] ClassRegistry& classes() noexcept { return m_classes; }
    // Held here rather than reached separately because every consumer that has
    // one reflection table wants the other: a property write validates an enum
    // value, and the binding that pushes one back out has to name its item.
    [[nodiscard]] const EnumRegistry& enums() const noexcept { return m_enums; }
    [[nodiscard]] EnumRegistry& enums() noexcept { return m_enums; }

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
    [[nodiscard]] ComponentPool<RigidBodyComponent>& rigidBodies() noexcept { return m_rigidBodies; }
    [[nodiscard]] const ComponentPool<RigidBodyComponent>& rigidBodies() const noexcept { return m_rigidBodies; }
    [[nodiscard]] ComponentPool<WeldComponent>& welds() noexcept { return m_welds; }
    [[nodiscard]] const ComponentPool<WeldComponent>& welds() const noexcept { return m_welds; }
    [[nodiscard]] ComponentPool<AttachmentComponent>& attachments() noexcept { return m_attachments; }
    [[nodiscard]] const ComponentPool<AttachmentComponent>& attachments() const noexcept { return m_attachments; }
    [[nodiscard]] ComponentPool<ConstraintComponent>& constraints() noexcept { return m_constraints; }
    [[nodiscard]] const ComponentPool<ConstraintComponent>& constraints() const noexcept { return m_constraints; }
    [[nodiscard]] ComponentPool<RagdollComponent>& ragdolls() noexcept { return m_ragdolls; }
    [[nodiscard]] const ComponentPool<RagdollComponent>& ragdolls() const noexcept { return m_ragdolls; }
    [[nodiscard]] ComponentPool<MaterialComponent>& materials() noexcept { return m_materials; }
    [[nodiscard]] const ComponentPool<MaterialComponent>& materials() const noexcept { return m_materials; }
    [[nodiscard]] ComponentPool<CharacterBodyComponent>& characterBodies() noexcept { return m_characterBodies; }
    [[nodiscard]] const ComponentPool<CharacterBodyComponent>& characterBodies() const noexcept
    {
        return m_characterBodies;
    }
    [[nodiscard]] ComponentPool<WorkspaceComponent>& workspaces() noexcept { return m_workspaces; }
    [[nodiscard]] const ComponentPool<WorkspaceComponent>& workspaces() const noexcept { return m_workspaces; }
    [[nodiscard]] ComponentPool<PVComponent>& pvInstances() noexcept { return m_pvInstances; }
    [[nodiscard]] const ComponentPool<PVComponent>& pvInstances() const noexcept { return m_pvInstances; }
    [[nodiscard]] ComponentPool<ModelComponent>& models() noexcept { return m_models; }
    [[nodiscard]] const ComponentPool<ModelComponent>& models() const noexcept { return m_models; }
    [[nodiscard]] ComponentPool<ScriptComponent>& scripts() noexcept { return m_scripts; }
    [[nodiscard]] const ComponentPool<ScriptComponent>& scripts() const noexcept { return m_scripts; }

    // The render module's classes (M4). Their storage is here and their meaning
    // is not: `scene` never includes `render`, and these five pools hold POD it
    // does not interpret. See components.h for why they are not behind a
    // type-erased registry.
    [[nodiscard]] ComponentPool<MeshPartComponent>& meshParts() noexcept { return m_meshParts; }
    [[nodiscard]] const ComponentPool<MeshPartComponent>& meshParts() const noexcept { return m_meshParts; }
    [[nodiscard]] ComponentPool<CameraComponent>& cameras() noexcept { return m_cameras; }
    [[nodiscard]] const ComponentPool<CameraComponent>& cameras() const noexcept { return m_cameras; }
    [[nodiscard]] ComponentPool<PointLightComponent>& pointLights() noexcept { return m_pointLights; }
    [[nodiscard]] const ComponentPool<PointLightComponent>& pointLights() const noexcept { return m_pointLights; }
    [[nodiscard]] ComponentPool<SpotLightComponent>& spotLights() noexcept { return m_spotLights; }
    [[nodiscard]] const ComponentPool<SpotLightComponent>& spotLights() const noexcept { return m_spotLights; }
    [[nodiscard]] ComponentPool<LightingComponent>& lighting() noexcept { return m_lighting; }
    [[nodiscard]] const ComponentPool<LightingComponent>& lighting() const noexcept { return m_lighting; }

    // The input module's classes (M6). Same arrangement as the render pools
    // above: the storage is here, the meaning is not.
    [[nodiscard]] ComponentPool<InputContextComponent>& inputContexts() noexcept { return m_inputContexts; }
    [[nodiscard]] const ComponentPool<InputContextComponent>& inputContexts() const noexcept { return m_inputContexts; }
    [[nodiscard]] ComponentPool<InputActionComponent>& inputActions() noexcept { return m_inputActions; }
    [[nodiscard]] const ComponentPool<InputActionComponent>& inputActions() const noexcept { return m_inputActions; }
    [[nodiscard]] ComponentPool<InputBindingComponent>& inputBindings() noexcept { return m_inputBindings; }
    [[nodiscard]] const ComponentPool<InputBindingComponent>& inputBindings() const noexcept { return m_inputBindings; }

    // The ui module's classes (M6).
    [[nodiscard]] ComponentPool<ScreenGuiComponent>& screenGuis() noexcept { return m_screenGuis; }
    [[nodiscard]] const ComponentPool<ScreenGuiComponent>& screenGuis() const noexcept { return m_screenGuis; }
    [[nodiscard]] ComponentPool<UIObjectComponent>& uiObjects() noexcept { return m_uiObjects; }
    [[nodiscard]] const ComponentPool<UIObjectComponent>& uiObjects() const noexcept { return m_uiObjects; }
    [[nodiscard]] ComponentPool<TextLabelComponent>& textLabels() noexcept { return m_textLabels; }
    [[nodiscard]] const ComponentPool<TextLabelComponent>& textLabels() const noexcept { return m_textLabels; }
    [[nodiscard]] ComponentPool<TextInputComponent>& textInputs() noexcept { return m_textInputs; }
    [[nodiscard]] const ComponentPool<TextInputComponent>& textInputs() const noexcept { return m_textInputs; }
    [[nodiscard]] ComponentPool<ImageLabelComponent>& imageLabels() noexcept { return m_imageLabels; }
    [[nodiscard]] const ComponentPool<ImageLabelComponent>& imageLabels() const noexcept { return m_imageLabels; }
    [[nodiscard]] ComponentPool<ScrollFrameComponent>& scrollFrames() noexcept { return m_scrollFrames; }
    [[nodiscard]] const ComponentPool<ScrollFrameComponent>& scrollFrames() const noexcept { return m_scrollFrames; }
    [[nodiscard]] ComponentPool<UIListLayoutComponent>& listLayouts() noexcept { return m_listLayouts; }
    [[nodiscard]] const ComponentPool<UIListLayoutComponent>& listLayouts() const noexcept { return m_listLayouts; }
    [[nodiscard]] ComponentPool<UIPaddingComponent>& uiPaddings() noexcept { return m_uiPaddings; }
    [[nodiscard]] const ComponentPool<UIPaddingComponent>& uiPaddings() const noexcept { return m_uiPaddings; }
    [[nodiscard]] ComponentPool<UICornerComponent>& uiCorners() noexcept { return m_uiCorners; }
    [[nodiscard]] const ComponentPool<UICornerComponent>& uiCorners() const noexcept { return m_uiCorners; }

    // The audio module's classes (M6).
    [[nodiscard]] ComponentPool<SoundComponent>& sounds() noexcept { return m_sounds; }
    [[nodiscard]] const ComponentPool<SoundComponent>& sounds() const noexcept { return m_sounds; }
    [[nodiscard]] ComponentPool<AudioGroupComponent>& audioGroups() noexcept { return m_audioGroups; }
    [[nodiscard]] const ComponentPool<AudioGroupComponent>& audioGroups() const noexcept { return m_audioGroups; }

private:
    // The pool walk `snapshot` and `restore` share, as `fn(worldPool,
    // snapshotPool)`. Templated on both sides so one body serves a `const
    // World&` copying out and a `World&` copying back, and each caller's lambda
    // decides the direction.
    template <class WorldRef, class SnapshotRef, class Fn>
    static void eachPool(WorldRef& world, SnapshotRef& snapshot, Fn&& fn)
    {
#define LUAUG_SCENE_POOL_VISIT(Type, Name) fn(world.m_##Name, snapshot.Name);
        LUAUG_SCENE_POOL_LIST(LUAUG_SCENE_POOL_VISIT)
#undef LUAUG_SCENE_POOL_VISIT
    }

    void linkChild(InstanceRecord& parentRecord, core::InstanceId parentId, core::InstanceId childId);
    // The general form: `beforeId` is the sibling the child is inserted ahead
    // of, and an invalid one means the end. `linkChild` is this with the end,
    // so an append and an insert cannot disagree about `firstChild`,
    // `lastChild` or the count.
    void linkChildBefore(InstanceRecord& parentRecord, core::InstanceId parentId, core::InstanceId childId,
                         core::InstanceId beforeId);
    void unlinkChild(core::InstanceId childId);
    void indexName(core::InstanceId parentId, core::InstanceId childId);
    // The same, for a rename: a renamed child may belong in the middle of a
    // chain, which the append above cannot express.
    void indexNameInChildOrder(core::InstanceId parentId, core::InstanceId childId);
    void unindexName(core::InstanceId parentId, core::InstanceId childId);

    ClassRegistry& m_classes;
    EnumRegistry& m_enums;
    core::AtomTable& m_atoms;
    core::Pcg32 m_rng;
    // Interned once so that `clone` can skip the one property that is structure
    // rather than a value, without a string compare per property per instance.
    // Declared after `m_atoms` because it is initialised from it, and the
    // initialiser list has to run in declaration order (-Wreorder-ctor).
    core::NameAtom m_parentProperty;
    // Declared after `m_atoms` for the same reason: its one group is named
    // "Default" and the atom for it comes from the table.
    CollisionGroups m_collisionGroups;

    core::SlotMap<InstanceRecord> m_instances;

    // Declared from the same list a snapshot's storage is, so the two cannot
    // drift. Declaration order follows the list, which is the order the pools
    // were added in over M2 through M7.
#define LUAUG_SCENE_POOL_MEMBER(Type, Name) ComponentPool<Type> m_##Name;
    LUAUG_SCENE_POOL_LIST(LUAUG_SCENE_POOL_MEMBER)
#undef LUAUG_SCENE_POOL_MEMBER

    // Insertion-ordered per tag, so `GetTagged` never leaks a hash order.
    std::unordered_map<u32, std::vector<core::InstanceId>> m_tagged;

    // Destroyed but not yet retired: their handles still resolve until the
    // drain that carries their `Destroying` finishes.
    std::vector<core::InstanceId> m_pendingRetire;

    EngineState m_engineState;
    ChangeQueue m_changes;
};

} // namespace luaug::scene
