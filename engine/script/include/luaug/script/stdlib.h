// What a sandboxed script can name, written down once (api-design.md §1.1).
//
// **Why this is a written list and not a generated one.** The names below belong
// to Luau, and Luau is pinned (R5) -- so the pin is the source of truth and this
// is a transcription of it, taken from `third_party/luau/VM/src/l*lib.cpp` rather
// than from memory. `api/defs/libraries.api.luau` refuses to declare these for
// exactly that reason ("re-declaring them would make this file a second source
// of truth for something the pin already fixes"), and the same argument applies
// here with one difference: the editor has to OFFER these names, and it cannot
// boot a VM to answer a keystroke.
//
// So the drift is caught instead of prevented. `sandbox_tests.cpp` stands up a
// real sandboxed VM and checks this list against it **in both directions** --
// every name here exists with the type it claims, and every key of every library
// table is named here. Bumping the Luau pin and gaining a function therefore
// fails a test rather than quietly leaving the editor a version behind.
//
// **It is this engine's surface, not stock Luau's.** `os` carries three names
// because `removeUnsafeGlobals` takes `difftime` off, and `getfenv`, `setfenv`,
// `newproxy` and `loadstring` are absent for the reasons `sandbox.cpp` gives.
// The list is what a script can actually reach.
//
// **Here rather than in the editor** because it is a fact about the VM. The
// script module is what owns the sandbox, what the test can boot, and what
// `engine/app` already depends on.
#pragma once

#include <span>
#include <string_view>

namespace luaug::script {

// One name a script can reach, and what `type()` answers for it.
//
// The type is carried rather than assumed because it is what makes the test
// worth running: a list that only checked existence would pass a `math.pi` that
// had become a function. It is also exactly the right-hand column of a
// completion row, so the editor gets it for nothing.
struct StdName
{
    std::string_view name;
    std::string_view type;
};

// A library table and everything in it.
struct StdLibrary
{
    std::string_view name;
    std::span<const StdName> members;
};

// The free globals -- Luau's base library as this sandbox leaves it. Does NOT
// include what the ENGINE installs (`print`, `warn`, `require`, `game`,
// `workspace`, `script`, `task` and the datatype namespaces): those are not
// Luau's and are not in the VM this list is tested against.
[[nodiscard]] std::span<const StdName> stdGlobals() noexcept;

// The ten library tables, in the order api-design.md §1.1 lists them.
[[nodiscard]] std::span<const StdLibrary> stdLibraries() noexcept;

} // namespace luaug::script
