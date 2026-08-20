#include "luaug/core/name_atom.h"

namespace luaug::core {

AtomTable::AtomTable()
{
    // Slot 0 is the empty/absent name, seeded here so that `text()` can index
    // unconditionally and so that no real string ever lands on the id that
    // `NameAtom::valid()` reports as absent.
    //
    // It is deliberately NOT put in `m_byText`: `intern` and `lookup` answer the
    // empty string before they ever reach the map, so an entry there would only
    // be a key nothing can ever match.
    m_texts.emplace_back();
}

NameAtom AtomTable::intern(std::string_view text)
{
    if (text.empty())
        return NameAtom{};

    if (const auto existing = m_byText.find(text); existing != m_byText.end())
        return NameAtom{existing->second};

    // The key is a view into the string that was just appended, which stays
    // valid for the life of the table because `m_texts` is a deque -- see the
    // header for why that choice is not interchangeable with a vector.
    m_texts.emplace_back(text);
    const u32 id = static_cast<u32>(m_texts.size() - 1);
    m_byText.emplace(std::string_view{m_texts[id]}, id);
    return NameAtom{id};
}

NameAtom AtomTable::lookup(std::string_view text) const noexcept
{
    if (text.empty())
        return NameAtom{};

    const auto existing = m_byText.find(text);
    return existing != m_byText.end() ? NameAtom{existing->second} : NameAtom{};
}

std::string_view AtomTable::text(NameAtom atom) const noexcept
{
    // An id past the end is a handle from another table or a hand-built value;
    // it reads as the empty name rather than as a bounds violation, because the
    // callers of this are logs and error messages that must not themselves fail.
    if (atom.id >= m_texts.size())
        return {};
    return m_texts[atom.id];
}

} // namespace luaug::core
