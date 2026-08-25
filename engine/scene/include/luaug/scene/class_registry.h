// Class reflection (architecture.md §4, ADR 0028).
//
// Every descriptor in here is GENERATED from `api/defs/*.api.luau` by
// `api/generator/gen_cpp.luau`. Hand-writing one is not a shortcut, it is a
// second source of truth: the same definition files also produce the type
// definitions the analyzer reads and the documentation users read, and the
// three only agree because none of them is written twice.
//
// What is hand-written is the small set of native functions the descriptors
// point at, and the tree operations on `World` that the bindings call directly.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/text_key.h"
#include "luaug/core/types.h"
#include "luaug/scene/types.h"
#include "luaug/scene/value.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace luaug::scene {

class World;

// 0 is "no class", so a zero-initialised `ClassComp` is detectably empty rather
// than accidentally an Instance -- the same convention `InstanceId` and
// `NameAtom` use.
using ClassId = u16;
inline constexpr ClassId InvalidClass = 0;

enum class ClassFlags : u8
{
    None = 0,
    // Reached through `game:GetService`, one per world.
    Service = 1 << 0,
    // Has no instances of its own; only its descendants do.
    Abstract = 1 << 1,
    // `Instance.new` refuses it (`scene.err.not_creatable`).
    NotCreatable = 1 << 2,
    // Compiled out of shipping builds.
    DevOnly = 1 << 3,
};

[[nodiscard]] constexpr ClassFlags operator|(ClassFlags a, ClassFlags b) noexcept
{
    return static_cast<ClassFlags>(static_cast<u8>(a) | static_cast<u8>(b));
}

[[nodiscard]] constexpr bool hasFlag(ClassFlags flags, ClassFlags flag) noexcept
{
    return (static_cast<u8>(flags) & static_cast<u8>(flag)) != 0;
}

// Carried from the IDL from the first definition, even though v1 runs every
// handler serially on the game VM (architecture.md §3). The checker enforces it
// as if the parallel windows were real, which is the only way code written now
// survives them arriving.
enum class ThreadSafety : u8
{
    Unsafe,
    ReadParallel,
    LocalSafe,
    Safe,
};

// Accessors are plain function pointers rather than std::function: there is one
// per property per class, they are generated, and a descriptor table that fits
// in cache is the difference between the Instance facade being free and being
// architecture risk #1.
struct PropertyDesc
{
    core::NameAtom name{};
    ValueType type = ValueType::Nil;

    // For `ValueType::EnumItem`, the NAME of the enum this property accepts;
    // an empty atom for every other type. `EnumRegistry::findId` turns it into
    // an `EnumId`.
    //
    // A name and not an id, for the reason `world_hash.cpp` hashes a class by
    // name: an `EnumId` is assigned by `EnumRegistry::registerEnum` in
    // registration order, so it is a fact about one registry rather than about
    // the property. These descriptor arrays have static storage and are shared
    // by every registry a process builds -- a test fixture's among them -- so an
    // id baked in here would name a different enum in the second one. The
    // generated `*EnumId` constants stay what a hand-written accessor validates
    // against; this costs one hash probe, and only on the path that populates a
    // combo box.
    //
    // Recorded at all because the alternative is recovering the enum from a
    // property's CURRENT VALUE, which cannot answer for a property that has none
    // and cannot answer at all without a live instance. A property's legal
    // domain is a fact about the class.
    core::NameAtom enumName{};

    // For a `Content` property, which KIND of file it may name -- `Mesh`,
    // `Texture`, `Audio`, `Font` -- and an empty atom for everything else.
    //
    // The same shape as `enumName` above and for the same argument. A `Content`
    // is a string as far as the engine is concerned, because it is a URI and
    // resolving it belongs to the mount; nothing downstream can tell a sound's
    // from a mesh's. That is fine for the runtime and useless for a person, and
    // the editor's picker is the caller: a list offering every file in the
    // project would be a file dialog with extra steps.
    //
    // An atom rather than an enum for the reason the enum name is one: these
    // arrays are static and shared by every registry a process builds, so
    // anything assigned at registration time would mean something different in
    // the second one.
    core::NameAtom contentKind{};

    // For an `Instance` property, which CLASS it may name -- `Material`,
    // `Attachment` -- and an empty atom when it may name any (`Weld.Part0`) or
    // is not a reference at all.
    //
    // The third of these, and the same argument as the two above: which
    // instances a property may point at is a fact about the class, and the value
    // -- one id -- can never answer it. A property that has none cannot be asked
    // at all.
    //
    // The editor's picker is the caller, and without this it had nothing to
    // build a list from: an instance reference was shown as text with a `go`
    // button and could not be SET, so `BasePart.Material` was a property the
    // property grid could only read. Offering every instance in the world would
    // be the file-dialog-with-extra-steps `contentKind` exists to avoid, and
    // offering a `Part` where a `Material` belongs is a write the setter refuses
    // -- a list making a promise the world will not keep.
    core::NameAtom instanceClass{};

    // **This string is CODE**, and a person edits it somewhere other than a
    // one-line field. Set by the IDL for `BaseScript.Source` and nothing else.
    //
    // A fact about the class for the reason `contentKind` is one: a string with
    // newlines in it might be Luau or might be an address, and a grid that
    // decided from the bytes would change its mind about a property the first
    // time somebody pressed Enter.
    bool code = false;

    // Whether a change to this property reaches the change queue even when
    // nothing has subscribed to it.
    //
    // **The default is the quiet path**: `setProperty` enqueues only for a
    // subscriber, which is what makes ten thousand parts moving every tick cost
    // nothing while nobody is listening. This is the exception for the few
    // properties the ENGINE has to notice regardless -- `Script.Enabled`, whose
    // write starts or stops a script (ADR 0059). Their writes are rare, so the
    // entry is free; a property that changed every tick must not have this.
    bool observed = false;

    ThreadSafety threadSafety = ThreadSafety::Unsafe;
    bool readOnly = false;
    // Backed, stored, read back faithfully -- and nothing acts on it yet. Not
    // the same as unbacked: an inert property round-trips perfectly, so every
    // way of checking it from a script agrees with what was written, which is
    // exactly what makes it hard to notice. The inspector shows it, because all
    // three of M4's cases were found by a human changing a value and watching
    // nothing happen.
    bool inert = false;

    // The property's own documentation, in English, from the IDL. Points at
    // generated static storage and is never null: `""` is what a hand-built
    // descriptor carries, so a caller needs no null check before printing it.
    //
    // Prose and not a `core::TextKey`, which is what this field was until a
    // property grid needed to show it. Three reasons, and the first is the one
    // that decides it:
    //
    //   * R3 governs what a PLAYER reads. This is API reference text, read by
    //     whoever is building the engine or a game -- the same audience
    //     `debug_overlay.h` states its own literals are for.
    //   * `gen_dts` and `gen_reference` already emit these sentences verbatim
    //     from the same IDL field. A catalog key would make the catalog a second
    //     home for prose that is generated from the IDL either way, and two
    //     homes for one sentence is what generating it was meant to prevent.
    //   * `i18n/en.json` is loaded at boot by every shipped game. Putting the
    //     whole API reference in it would cost every player the documentation
    //     for a panel shipping builds do not draw -- and a `TextKey` carries
    //     only a hash, so a tool holding this descriptor without that catalog
    //     would have nothing at all.
    const char* doc = "";

    // Raised when `set` rejects the value. Generated from the property's type,
    // so every rejection names something specific rather than "invalid value".
    core::TextKey errKeyOnInvalidSet{};

    Value (*get)(const World&, core::InstanceId) = nullptr;

    // Returns false and leaves the instance untouched when the value is out of
    // domain; the caller raises `errKeyOnInvalidSet`. Returning a bool rather
    // than raising keeps `scene` free of the error-formatting path, which lives
    // above it.
    bool (*set)(World&, core::InstanceId, const Value&) = nullptr;
};

// Methods are declared here and implemented in `script`. `scene` cannot call
// one: a method takes and returns Luau values, and L3 has no VM. What this
// entry buys is a **cross-check at boot** -- every method the IDL declares must
// have a binding registered for it, and every registered binding must be
// declared. A method that exists in one and not the other is a mismatch found
// at startup rather than the first time a script calls it.
struct MethodDesc
{
    core::NameAtom name{};
    bool yields = false;
    ThreadSafety threadSafety = ThreadSafety::Unsafe;
    // English prose from the IDL, never null. See `PropertyDesc::doc`.
    const char* doc = "";
};

struct EventDesc
{
    core::NameAtom name{};
    // Index into the instance's signal slots, assigned by the generator so that
    // a fire is an array offset rather than a name lookup.
    u16 slot = 0;
    // English prose from the IDL, never null. See `PropertyDesc::doc`.
    const char* doc = "";
};

struct ClassDescriptor
{
    core::NameAtom name{};
    // `InvalidClass` only for `Instance` itself.
    ClassId super = InvalidClass;
    ClassFlags flags = ClassFlags::None;
    // The `Name` a fresh instance carries; the class name unless the IDL says
    // otherwise.
    core::NameAtom defaultName{};
    // English prose from the IDL, never null. See `PropertyDesc::doc`.
    const char* doc = "";

    // Views into generated static storage, which outlives the registry. The
    // arrays hold only what the class *declares*; inherited members are found
    // by walking `super`, and the registry caches that walk.
    std::span<const PropertyDesc> properties{};
    std::span<const MethodDesc> methods{};
    std::span<const EventDesc> events{};

    // Attaches and detaches the components this class *declares*, and only
    // those. `World::create` walks the ancestry root-first and calls each, so a
    // `Part` gets `BasePart`'s component from `BasePart`'s hook rather than
    // from a switch in `create` that would have to know every class in the
    // engine. Null for a class that stores nothing of its own, which is most of
    // them.
    void (*attachComponents)(World&, core::InstanceId) = nullptr;
    void (*detachComponents)(World&, core::InstanceId) = nullptr;
};

// Deliberately does NOT hold an `AtomTable`. It stores atoms and compares
// atoms; resolving one back to text is the caller's business, and a reference
// it never reads would be a dependency that looks load-bearing in every
// constructor call. Clang's `-Wunused-private-field` is what pointed this out,
// on a field MSVC was happy to keep forever.
class ClassRegistry
{
public:
    // Not defaulted: the constructor seeds the unused slot 0 that keeps
    // `ClassId` 0 meaning "no class".
    ClassRegistry();

    // Registration order must place a class after its superclass, which the
    // generator guarantees by emitting parents first. Returns `InvalidClass` if
    // the super is unknown or the name is already taken.
    ClassId registerClass(const ClassDescriptor& descriptor);

    [[nodiscard]] const ClassDescriptor* find(ClassId id) const noexcept;
    [[nodiscard]] ClassId findId(core::NameAtom name) const noexcept;

    // `derived` is `base`, or descends from it. This is `Instance:IsA`, and it
    // is a flat lookup rather than a walk: the ancestor set is materialised at
    // registration because it is read far more often than it is built.
    [[nodiscard]] bool isA(ClassId derived, ClassId base) const noexcept;

    // Resolved through the hierarchy and memoised, so a property inherited from
    // `Instance` costs the same as one declared on the class. Returns nullptr
    // for a name the class does not have, which is what raises
    // `scene.err.unknown_member` above.
    [[nodiscard]] const PropertyDesc* findProperty(ClassId id, core::NameAtom name) const noexcept;

    // The property's dense index within this class, inherited members included
    // and counted first. `NoSlot` for a name the class does not have.
    //
    // A pointer offset into the descriptor array cannot serve here, and getting
    // that wrong is silent: a property declared on `BasePart` lives in
    // `BasePart`'s array, so measuring it against `Part`'s produces a garbage
    // index. That made every inherited property permanently unsubscribable and
    // permanently noisy -- found by a test, not by reading.
    static constexpr u16 NoSlot = 0xFFFFu;
    [[nodiscard]] u16 propertySlot(ClassId id, core::NameAtom name) const noexcept;
    [[nodiscard]] const MethodDesc* findMethod(ClassId id, core::NameAtom name) const noexcept;
    [[nodiscard]] const EventDesc* findEvent(ClassId id, core::NameAtom name) const noexcept;

    [[nodiscard]] usize classCount() const noexcept { return m_classes.size(); }

private:
    struct PropertyEntry
    {
        const PropertyDesc* descriptor = nullptr;
        // Stable across the hierarchy: a class keeps its super's numbering and
        // appends its own after it, so `Name` is slot 0 on every class that
        // inherits it.
        u16 slot = NoSlot;
    };

    struct Entry
    {
        ClassDescriptor descriptor;
        // Every ancestor including itself, so `isA` is one hash probe.
        std::vector<ClassId> ancestry;
        std::unordered_map<u32, PropertyEntry> properties;
        std::unordered_map<u32, const MethodDesc*> methods;
        std::unordered_map<u32, const EventDesc*> events;
    };

    // Index 0 is unused so that `ClassId` 0 stays invalid.
    std::vector<Entry> m_classes;
    std::unordered_map<u32, ClassId> m_byName;
};

} // namespace luaug::scene
