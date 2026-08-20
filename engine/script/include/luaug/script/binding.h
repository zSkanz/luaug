// The rules every Luau binding in this engine obeys (architecture.md §5).
//
// Grounded in `docs/research/luau-c-api-2026.md`, which was written by reading
// the vendored 0.734 sources rather than by remembering how Luau works. Four of
// the constraints below are not style choices -- they are properties of the VM
// that make a binding either fast or broken, and each cost a reading of
// `third_party/luau` to establish.
//
//   1. **One tag per userdata TYPE, and a tag has exactly one metatable.**
//      `lua_setuserdatametatable` refuses to reassign, so per-class metatables
//      for Instance classes are impossible: there is one `Instance` tag and the
//      class is resolved from the `InstanceId` (M2 brief, Decision 13). Host
//      tags run 0..127 and are shared with every datatype, so they are a budget
//      rather than a namespace -- and tag 0 is what plain `lua_newuserdata`
//      produces, so it is reserved.
//
//   2. **Everything registers before the first script loads.** The fast
//      property-dispatch opcodes are chosen once per `Proto` at `luau_load`,
//      and a deopt is permanent and one-way. A metatable installed after a
//      chunk is loaded is a metatable that chunk will reach only through the
//      slow path, forever.
//
//   3. **The `useratom` callback is installed before `luaL_openlibs`.** It is
//      lazy and one-shot per string, and a string interned before the callback
//      exists latches at -1 for the life of the VM. Installing it late is
//      invisible until a property lookup silently stops using its atom.
//
//   4. **`luaL_sandbox` removes nothing.** It freezes one level deep and only
//      the string metatable, whatever its own comment says. Every removal R4
//      implies -- `getfenv`, `loadstring`, `wait` and the rest -- is ours to
//      perform, before the freeze.
#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"
#include "luaug/scene/class_registry.h"

struct lua_State;
typedef int (*lua_CFunction)(lua_State* L);

namespace luaug::scene
{
class World;
}

namespace luaug::script
{

class SignalSystem;
class TaskScheduler;
class ServiceState;

using core::f32;
using core::f64;
using core::i16;
using core::i32;
using core::i64;
using core::u16;
using core::u32;
using core::u64;
using core::u8;
using core::usize;

// The full-userdata tag budget is 128 and it is shared, so this list is the
// engine's whole allocation. Values are explicit because a tag is written into
// every userdata header: reordering them is an ABI change to anything that
// persisted one, which is why architecture.md §5 says never to persist one.
enum class UserdataTag : int
{
    // 0 is what plain `lua_newuserdata` produces, including from any third-party
    // C library linked into the same VM. Claiming it would make our objects
    // indistinguishable from theirs.
    Reserved = 0,

    Instance = 1,
    CFrame = 2,
    Color3 = 3,
    Random = 4,
    Signal = 5,
    Connection = 6,
    EnumItem = 7,

    // An enum object -- `Enum.PartShape` -- and the `Enum` global itself. Both
    // would read more naturally as frozen tables, and both are userdata for one
    // reason: `typeof` reports a metatable's `__type` for userdata and
    // deliberately does NOT for a table (`ltm.cpp:167`, "for all types except
    // userdata and table"). api-design.md §2.3 requires `typeof(Enum.PartShape)`
    // to be "Enum" and `typeof(Enum)` to be "Enums", and a table cannot answer
    // either. One tag covers every enum object, the same way one covers every
    // Instance class: the payload is the `EnumId`.
    Enum = 8,
    Enums = 9,

    // Not a tag. The count exists so a registration loop can assert it covered
    // everything, and so the budget remaining is a number someone can read.
    Count,
};

// `Vector3` is deliberately absent: it IS the native `vector` primitive
// (ADR 0013), never userdata. A tag for it would mean the fastcalls and the
// constant folding were not happening.
static_assert(static_cast<int>(UserdataTag::Count) <= 128, "host userdata tags run 0..127");

// The payload behind `UserdataTag::Instance`. Eight bytes, and the class is not
// in it: resolving the class from the id through the registry is what buys one
// tag instead of one per class (Decision 13).
struct InstanceUserdata
{
    core::InstanceId id;
};

static_assert(sizeof(InstanceUserdata) == 8, "the Instance payload's size is an ABI decision");

// Interned property and method names, resolved once by the `useratom` callback
// and compared as integers thereafter. This is the same trick `core::AtomTable`
// plays engine-side, and the two are deliberately separate: Luau's atoms are
// assigned by the VM and ours by the engine, and conflating them would make an
// engine atom depend on which strings a script happened to mention.
//
// The mapping from a Luau atom to an engine `NameAtom` is a table the runtime
// fills as the VM interns each name.
using LuauAtom = i16;

// A member name and the C function that answers it. Datatype bindings dispatch
// by scanning their tag's table, and scanning is the right shape here: no
// datatype in the surface has more than a dozen members, so the whole table
// fits in a cache line or two and a hash probe would cost more than it saves.
//
// Instances do NOT use these tables. A class's members are resolved through
// `scene::ClassRegistry`, which memoises the inheritance walk -- that is the
// path architecture risk #1 is measured on, and it is a hash probe by design.
struct MemberEntry
{
    core::NameAtom name;
    lua_CFunction fn = nullptr;
};

using MemberTable = std::vector<MemberEntry>;

// Everything a C binding needs and cannot be handed as an argument, reachable
// from any `lua_CFunction` through `lua_callbacks(L)->userdata` -- which
// `lua.h:601` guarantees Luau never overwrites.
//
// This is per-VM rather than process-global, and that is not a refinement: it
// is what `useratom`'s signature already allows. The callback receives the
// `lua_State`, so it can reach this the same way every other binding does, and
// the "exactly one game VM" limitation an earlier draft wrote down was a
// property of that draft rather than of the VM.
struct VmContext
{
    scene::World* world = nullptr;

    // Owned by `ScriptRuntime`, whose lifetime is the only one that brackets the
    // `lua_State`. Pointers rather than values so that `binding.h` -- which
    // every binding includes -- does not have to carry the queue's definition.
    SignalSystem* signals = nullptr;
    TaskScheduler* tasks = nullptr;
    ServiceState* services = nullptr;

    // Indexed by Luau atom; holds the engine `NameAtom` id for the same text.
    // Grown by `useratom` as the VM interns each name, and never shrunk: an
    // atom is assigned once per string for the life of the state.
    std::vector<u32> atomToName;

    // Per tag. Populated at boot, before the sandbox and before any script
    // loads, because a metatable registered later is one already-loaded chunks
    // reach only through the slow path (rule 2 above).
    MemberTable getters[static_cast<usize>(UserdataTag::Count)];
    MemberTable methods[static_cast<usize>(UserdataTag::Count)];

    // An Instance method's implementation, keyed by the descriptor the IDL
    // generated for it. Keyed by the pointer rather than by name because a
    // method name is only unique within a class -- `Clone` is declared on
    // `Instance` and on `Random` -- and the descriptor is the thing that is
    // already unique, already resolved through the inheritance walk, and
    // already in static storage that outlives the VM.
    //
    // It is also the cross-check `MethodDesc` exists for: at boot, a declared
    // method with no entry here and an entry with no declaration are both
    // reported, so a surface that ships ahead of its implementation is a number
    // at startup rather than a nil discovered by a script.
    std::unordered_map<const scene::MethodDesc*, lua_CFunction> instanceMethods;

    // Hand-written bindings whose method no definition declares. Counted as the
    // batches land rather than walked for afterwards, because a stale entry has
    // no descriptor for a walk to find it by.
    usize unboundDeclarations = 0;

    // Registry ref of the weak-valued table that gives `a == b` for two handles
    // to the same instance. Keyed by the id's `index`, which is dense from the
    // slot map and therefore lands in a Luau table's array part.
    int instanceCacheRef = -1;

    // Names a binding compares against on a path that runs per property write.
    // Interned once at boot rather than looked up per call: `AtomTable::lookup`
    // is a hash probe over a string, and the 10k-parts benchmark is measured on
    // exactly this path.
    struct WellKnownAtoms
    {
        core::NameAtom parent;
        core::NameAtom cframe;
    } wellKnown;

    // Takes the `int` the Luau API hands out rather than a `LuauAtom`: every
    // call site gets one from `lua_tostringatom` or `lua_namecallatom`, and
    // narrowing it at each of them is a cast per property access that can only
    // ever lose information.
    //
    // Invalid for -1, which is what Luau returns for a string interned before
    // the callback existed, and for an atom past the end of the table.
    [[nodiscard]] core::NameAtom resolve(int atom) const noexcept;
};

// Never null after `ScriptRuntime::boot`; calling a binding on a state that has
// none is a programming error rather than a runtime condition, so this asserts
// rather than branching on every property access.
[[nodiscard]] VmContext& context(lua_State* L) noexcept;

[[nodiscard]] const MemberEntry* findMember(const MemberTable& table, core::NameAtom name) noexcept;

// Creates the tag's metatable, registers it, and installs the shared
// `__index`/`__newindex`/`__namecall` that dispatch through the tables above.
// The metatable is left on the stack for the caller to add its own metamethods
// to; `endTagMetatable` freezes and pops it.
//
// Registration first and population second, because the metatable is stored by
// pointer and later mutation is visible (`lapi.cpp:1583`) -- the vendored
// conformance suite does exactly this. The freeze at the end is ours to do:
// `luaL_sandbox` never touches a tag metatable, so without it a script that
// reaches one through `getmetatable` could rewrite the type.
void beginTagMetatable(lua_State* L, UserdataTag tag);
void endTagMetatable(lua_State* L);

// The common case: no metamethods beyond the shared three plus whichever of
// `__eq` and `__tostring` the type wants. Either may be null.
void installTagMetatable(lua_State* L, UserdataTag tag, lua_CFunction equals, lua_CFunction tostring);

// Raises a Luau error carrying the key-prefixed catalog text, which is what
// lets a conformance spec match on a stable identifier while the prose stays
// free to be translated (`core::makeError`, ADR 0019). Never returns.
[[noreturn]] void raise(lua_State* L, core::TextKey key, std::span<const core::I18nArg> args = {});

// The `__type` a tag's metatable reports, which is what `typeof` answers
// (api-design.md §2.3). Shared so that the name a value reports and the name an
// error message uses cannot drift apart.
[[nodiscard]] const char* typeName(UserdataTag tag) noexcept;

// `script.err.unknown_member`, naming the type it was reached through. Reading
// a member a datatype does not have is an error and not `nil`: that is what
// makes the §2.5 renames enforceable rather than silently returning nothing.
[[noreturn]] void raiseUnknownMember(lua_State* L, UserdataTag tag, const char* member);

} // namespace luaug::script
