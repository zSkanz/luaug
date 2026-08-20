// The Instance facade as scripts see it (architecture.md §5, api-design.md
// §2.2).
//
// **One tag, one metatable, and the class resolved from the id** (M2 brief,
// Decision 13). Luau allows exactly one metatable per userdata tag and refuses
// reassignment, so per-class metatables are not available at any price; and the
// dispatch cache the fast opcodes use lives in the bytecode instruction, which
// makes it per *callsite* and shared by every tag flowing through it. A slot
// numbering that is a pure function of the name is safe there and a per-class
// one mis-dispatches the first time a callsite sees two classes.
//
// So `__index` resolves `InstanceId -> ClassId -> descriptor` and then switches
// on the name atom. The extra indirection lands on a path that already touches
// the component store.
#pragma once

#include "luaug/core/id.h"
#include "luaug/script/binding.h"

#include <span>

struct lua_State;

namespace luaug::script {

// One method implementation, named by the class that DECLARES it. Binding it to
// the declaring class is what makes inheritance work for free: the registry
// resolves `part:Destroy()` to `Instance`'s descriptor, and the implementation
// table is keyed by descriptor.
struct InstanceMethodBinding
{
    const char* className = nullptr;
    const char* methodName = nullptr;
    lua_CFunction fn = nullptr;
};

// Installs the Instance metatable, the `Instance` global, and the methods
// `Instance` and `Model` declare. Runs during boot, after the class registry is
// populated and before the sandbox.
void registerInstanceBinding(lua_State* L);

// Binds a batch of implementations. Split from the registration above so that a
// class's methods live beside the rest of that class's binding -- the services
// implement their own in `services.cpp` -- rather than in one table that every
// module has to reach into.
//
// A binding whose method no definition declares is counted and skipped: nothing
// generated that surface, so it is a stale hand-written entry rather than a new
// feature.
void bindInstanceMethods(lua_State* L, std::span<const InstanceMethodBinding> bindings);

// The two halves of the cross-check `MethodDesc` exists for.
// `declaredWithoutBinding` is how many methods the IDL declares that this build
// does not implement, and those raise `script.err.not_implemented` rather than
// reading as a missing member. `boundWithoutDeclaration` should always be zero.
//
// Computed at the end of boot rather than during it, because a method declared
// on one class may be bound by another module later in the sequence.
struct MethodCoverage
{
    usize declared = 0;
    usize bound = 0;
    usize declaredWithoutBinding = 0;
    usize boundWithoutDeclaration = 0;
};

[[nodiscard]] MethodCoverage methodCoverage(lua_State* L);

// Pushes nil for an invalid id, and otherwise the ONE userdata this VM uses for
// that instance -- the weak-valued cache is what makes `a == b` true for two
// handles to the same thing, which scripts rely on without ever thinking about
// it.
void pushInstance(lua_State* L, core::InstanceId id);

// Null when the value is not an Instance. Does not check whether the instance
// is still alive: a destroyed handle is a legitimate thing to hold and to
// compare, and only *using* one raises (api-design.md divergence #25).
[[nodiscard]] const core::InstanceId* toInstance(lua_State* L, int index) noexcept;

// Raises naming the type it wanted, and then raises again if the instance has
// been destroyed -- so every call site that needs a live instance gets both
// checks from one call.
[[nodiscard]] core::InstanceId checkInstance(lua_State* L, int index);

} // namespace luaug::script
