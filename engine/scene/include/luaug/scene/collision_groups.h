// The collision-group table (api-design.md §2.1, `PhysicsService`).
//
// It lives in `scene` rather than in the physics backend, and that is a
// deliberate split rather than duplication for its own sake. The names and the
// matrix are things a script writes, reads back and replays -- so they are
// world state, they belong to the world's lifetime, and they are what
// `BasePart.CollisionGroup` is validated against. The backend keeps its own
// copy because its filter is consulted from inside the solver, where reaching
// back into the scene is not an option.
//
// Small on purpose: the table is square and one byte per pair, so the four
// groups a game actually registers cost sixteen bytes. The bound exists so a
// script cannot ask for a megabyte by looping.
#pragma once

#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"

#include <vector>

namespace luaug::scene {

using core::u16;
using core::u32;
using core::u8;
using core::usize;

class CollisionGroups
{
public:
    static constexpr u16 kInvalid = 0xffffu;
    static constexpr u16 kDefault = 0;
    // Ten bits, which is what a 16-bit object layer leaves once the broad phase
    // has taken one for moving-versus-static.
    static constexpr u32 kMaxGroups = 1024;

    // `Default` exists from construction and is never removed, so a part that
    // was never told which group it is in is in a real one.
    explicit CollisionGroups(core::NameAtom defaultName) : m_names{defaultName}, m_collidable{1} {}

    [[nodiscard]] u32 count() const noexcept { return static_cast<u32>(m_names.size()); }
    [[nodiscard]] core::NameAtom nameAt(u16 group) const noexcept { return m_names[group]; }

    [[nodiscard]] u16 find(core::NameAtom name) const noexcept
    {
        for (usize i = 0; i < m_names.size(); ++i) {
            if (m_names[i] == name)
                return static_cast<u16>(i);
        }
        return kInvalid;
    }

    // Idempotent: registering a name that exists returns it rather than failing,
    // which is what lets a script register its groups at file scope and survive
    // a hot reload.
    [[nodiscard]] u16 add(core::NameAtom name)
    {
        if (const u16 existing = find(name); existing != kInvalid)
            return existing;
        if (m_names.size() >= kMaxGroups)
            return kInvalid;

        const u32 previous = count();
        const u32 next = previous + 1;
        // A new group collides with everything until told otherwise, which is
        // the only default that cannot surprise anyone: the alternative is a
        // group whose parts fall through the world until a second call is made.
        std::vector<u8> grown(static_cast<usize>(next) * next, 1);
        for (u32 row = 0; row < previous; ++row) {
            for (u32 column = 0; column < previous; ++column)
                grown[static_cast<usize>(row) * next + column] =
                    m_collidable[static_cast<usize>(row) * previous + column];
        }
        m_collidable.swap(grown);
        m_names.push_back(name);
        return static_cast<u16>(previous);
    }

    // Symmetric, because a one-way collision is not something a solver can
    // express: if A does not collide with B then B does not collide with A.
    void setCollidable(u16 a, u16 b, bool collidable) noexcept
    {
        if (a >= count() || b >= count())
            return;
        m_collidable[static_cast<usize>(a) * count() + b] = collidable ? u8{1} : u8{0};
        m_collidable[static_cast<usize>(b) * count() + a] = collidable ? u8{1} : u8{0};
    }

    [[nodiscard]] bool collidable(u16 a, u16 b) const noexcept
    {
        if (a >= count() || b >= count())
            return false;
        return m_collidable[static_cast<usize>(a) * count() + b] != 0u;
    }

    // Monotone, and the mirror reads it to know whether it has anything to push
    // down. Cheaper than diffing a table that changes twice in a game's life.
    [[nodiscard]] u32 revision() const noexcept { return m_revision; }
    void bumpRevision() noexcept { ++m_revision; }

private:
    std::vector<core::NameAtom> m_names;
    std::vector<u8> m_collidable;
    u32 m_revision = 0;
};

} // namespace luaug::scene
