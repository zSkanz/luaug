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

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

struct lua_State;

namespace luaug::script
{

using core::f32;
using core::f64;
using core::i16;
using core::i32;
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

} // namespace luaug::script
