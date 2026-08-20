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

#include "luaug/core/types.h"

#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace luaug::core {

// Atom 0 is the empty/absent name, so a zero-initialised `NameAtom` is absent
// and `valid()` is one comparison -- the same convention `InstanceId` uses.
struct NameAtom
{
    u32 id = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return id != 0; }
    [[nodiscard]] constexpr bool operator==(const NameAtom&) const noexcept = default;
};

// Append-only: an interned string is never removed, and appending never moves
// an existing one. That is what lets callers hold a `string_view` for the
// lifetime of the table, and it is why the table is only ever engine-wide and
// long-lived rather than per-object.
//
// The storage is a `deque`, and that is load-bearing rather than a preference.
// A `vector<string>` moves every element when it grows, and a short string
// (SSO) carries its characters *inside* the string object -- so every view into
// one dangles after a reallocation. The failure mode is not a crash: it is a
// lookup miss, which makes `intern` mint a second atom for a name that already
// had one, destroying the exact equality the child-name index and property
// dispatch are built on. Instance and property names are overwhelmingly short,
// so this would have been the common case rather than the rare one.
// `deque::push_back` does not invalidate references to existing elements.
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
    std::deque<std::string> m_texts;
    // Keys are views into `m_texts`, which is safe precisely because appending
    // to a deque leaves every existing element where it is.
    std::unordered_map<std::string_view, u32> m_byText;
};

} // namespace luaug::core
