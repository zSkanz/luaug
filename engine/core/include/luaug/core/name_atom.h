// Interned names (architecture.md §2, §4).
//
// Instance names, property names and tags are compared and looked up far more
// often than they are created, so they are interned once into a `u32` and
// compared as integers thereafter. The child-name index (ADR 0026) and the
// property dispatch table are both keyed on atoms.
//
// **The one rule that matters: an atom's numeric value is not data.** It
// depends on the order strings happened to be interned, which depends on the
// order things happened to be created. Hashing an atom, serialising one, or
// sorting by one puts that order into observable state and breaks determinism
// (R10, ADR 0025) in a way that reproduces perfectly on your machine and fails
// somewhere else. Hash the *text*. Sort by the *text*. The atom is a fast
// comparison key and nothing else.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "luaug/core/types.h"

namespace luaug::core
{

// Atom 0 is the empty/absent name, so a zero-initialised `NameAtom` is absent
// and `valid()` is one comparison -- the same convention `InstanceId` uses.
struct NameAtom
{
    u32 id = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return id != 0; }
    [[nodiscard]] constexpr bool operator==(const NameAtom&) const noexcept = default;
};

// Append-only: an interned string is never removed, and the storage never
// reallocates its character data out from under a `text()` result. That is what
// lets callers hold a `string_view` for the lifetime of the table, and it is
// why the table is only ever engine-wide and long-lived rather than per-object.
class AtomTable
{
public:
    AtomTable();

    // Interns `text` if it is new. The empty string always returns atom 0.
    [[nodiscard]] NameAtom intern(std::string_view text);

    // Pure lookup -- returns an invalid atom rather than interning. Use this on
    // paths where a miss is a normal answer (`FindFirstChild` on a name nothing
    // in the world has ever been called), so that querying cannot grow the
    // table without bound.
    [[nodiscard]] NameAtom lookup(std::string_view text) const noexcept;

    // Empty for an invalid or unknown atom, never a dangling view.
    [[nodiscard]] std::string_view text(NameAtom atom) const noexcept;

    [[nodiscard]] usize size() const noexcept { return m_texts.size(); }

private:
    // Indexed by atom id; slot 0 holds the empty string so no branch is needed
    // on the common path.
    std::vector<std::string> m_texts;
    // Keys are views into `m_texts`, which is safe precisely because the table
    // is append-only and `std::string` storage does not move when a *different*
    // element is appended.
    std::unordered_map<std::string_view, u32> m_byText;
};

} // namespace luaug::core
