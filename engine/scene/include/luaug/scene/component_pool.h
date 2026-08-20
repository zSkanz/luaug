// Sparse-set component storage (architecture.md §4, ADR 0028).
//
// Three properties decide this design, and all three are why the ECS is
// hand-rolled rather than EnTT:
//
//   1. **Iteration is dense and in insertion order.** A system walks a
//      contiguous array, and it walks it in an order that is a pure function of
//      the operation sequence -- never a hash order, never an address order
//      (R10, ADR 0025).
//   2. **The dense array is POD and memcpy-able**, which is what makes
//      `World::snapshot()` a per-pool copy rather than a traversal
//      (ADR 0016's rollback-oriented foundations).
//   3. **Removal does not reorder.** A swap-and-pop would be faster and would
//      make iteration order depend on removal history, which is exactly the
//      dependency (1) rules out. Removal leaves a hole, and compaction happens
//      only at a FrameStart safe point where nothing is mid-iteration.
#pragma once

#include <vector>

#include "luaug/core/id.h"
#include "luaug/core/types.h"
#include "luaug/scene/types.h"

namespace luaug::scene
{

template <class T>
class ComponentPool
{
public:
    static constexpr u32 Absent = 0xFFFFFFFFu;

    // Overwrites an existing component rather than duplicating it, because
    // "attach this component" is idempotent from every caller's point of view.
    T& add(core::InstanceId id, T value)
    {
        ensureSparse(id.index);
        const u32 existing = m_sparse[id.index];
        if (existing != Absent && m_owners[existing] == id)
        {
            m_dense[existing] = std::move(value);
            return m_dense[existing];
        }

        m_dense.push_back(std::move(value));
        m_owners.push_back(id);
        m_sparse[id.index] = static_cast<u32>(m_dense.size() - 1);
        ++m_liveCount;
        return m_dense.back();
    }

    [[nodiscard]] T* find(core::InstanceId id) noexcept { return const_cast<T*>(constFind(id)); }
    [[nodiscard]] const T* find(core::InstanceId id) const noexcept { return constFind(id); }
    [[nodiscard]] bool contains(core::InstanceId id) const noexcept { return constFind(id) != nullptr; }

    bool remove(core::InstanceId id) noexcept
    {
        if (constFind(id) == nullptr)
            return false;
        const u32 slot = m_sparse[id.index];
        m_sparse[id.index] = Absent;
        // The hole keeps its position. `m_owners[slot]` is invalidated so the
        // slot reads as dead to iteration and to `find`, and the payload is
        // left alone rather than destroyed early -- these are POD components,
        // and a value nobody can reach is not a leak.
        m_owners[slot] = core::InstanceId{};
        --m_liveCount;
        return true;
    }

    // Visits live components in dense order as `fn(InstanceId, T&)`. Adding or
    // removing during a walk is not supported: collect first.
    template <class Fn>
    void forEach(Fn&& fn)
    {
        for (usize slot = 0; slot < m_dense.size(); ++slot)
        {
            if (m_owners[slot].valid())
                fn(m_owners[slot], m_dense[slot]);
        }
    }

    template <class Fn>
    void forEach(Fn&& fn) const
    {
        for (usize slot = 0; slot < m_dense.size(); ++slot)
        {
            if (m_owners[slot].valid())
                fn(m_owners[slot], m_dense[slot]);
        }
    }

    // Drops the holes and closes the dense array up, preserving the relative
    // order of what survives. Callers run this only at a FrameStart safe point:
    // it moves components, so any index held across it is stale.
    void compact()
    {
        usize write = 0;
        for (usize read = 0; read < m_dense.size(); ++read)
        {
            if (!m_owners[read].valid())
                continue;
            if (write != read)
            {
                m_dense[write] = std::move(m_dense[read]);
                m_owners[write] = m_owners[read];
            }
            m_sparse[m_owners[write].index] = static_cast<u32>(write);
            ++write;
        }
        m_dense.resize(write);
        m_owners.resize(write);
    }

    [[nodiscard]] usize size() const noexcept { return m_liveCount; }
    [[nodiscard]] usize denseSize() const noexcept { return m_dense.size(); }

    void clear() noexcept
    {
        m_dense.clear();
        m_owners.clear();
        m_sparse.clear();
        m_liveCount = 0;
    }

private:
    [[nodiscard]] const T* constFind(core::InstanceId id) const noexcept
    {
        if (!id.valid() || id.index >= m_sparse.size())
            return nullptr;
        const u32 slot = m_sparse[id.index];
        if (slot == Absent || slot >= m_owners.size())
            return nullptr;
        // The generation check is what stops a recycled slot from inheriting
        // the component of whatever used to live there.
        return m_owners[slot] == id ? &m_dense[slot] : nullptr;
    }

    void ensureSparse(u32 index)
    {
        if (index >= m_sparse.size())
            m_sparse.resize(static_cast<usize>(index) + 1, Absent);
    }

    // Indexed by `InstanceId::index`, so it is as large as the entity slot map
    // rather than as large as the component count. That is the sparse half.
    std::vector<u32> m_sparse;
    std::vector<T> m_dense;
    // Parallel to `m_dense`; an invalid id marks a hole.
    std::vector<core::InstanceId> m_owners;
    usize m_liveCount = 0;
};

} // namespace luaug::scene
