// Generation-checked slot storage (architecture.md §4, ADR 0028).
//
// This is the container the ECS hands out `InstanceId`s from. Two properties
// are load-bearing and neither is negotiable:
//
//   1. **Stale handles are detected, not dereferenced.** Freeing a slot bumps
//      its generation, so every handle to the old occupant stops resolving.
//   2. **Iteration is deterministic** -- ascending slot index, always. Nothing
//      here consults a hash, a pointer value, or an allocation address, because
//      simulation state must reproduce exactly (R10, ADR 0025).
//
// Note that ascending slot index is *not* insertion order once slots are
// reused, and callers must not assume it is. Determinism needs iteration to be
// a pure function of the operation sequence, which it is; anything that needs
// creation order has to record it explicitly, the way the instance hierarchy
// records sibling order in its own links.
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "luaug/core/id.h"
#include "luaug/core/types.h"

namespace luaug::core
{

// `Handle` is the id type this map hands out: any aggregate with a `u32 index`,
// a `u32 generation` and a `valid()`. It is a parameter rather than always
// `InstanceId` so that a map of something else -- signals, connections -- cannot
// hand out a value the Instance bindings would accept. Two handle types with
// identical layout still refuse to convert, which is the only thing standing
// between a slot number and the wrong container.
template <class T, class Handle = InstanceId>
class SlotMap
{
public:
    using HandleType = Handle;

    [[nodiscard]] Handle insert(T value)
    {
        if (m_freeHead != FreeListEnd)
        {
            const u32 index = m_freeHead;
            // Move in BEFORE unlinking. If `T`'s move throws, the slot is still
            // on the free list and nothing has leaked; unlinking first would
            // strand it permanently.
            m_slots[index] = std::move(value);
            m_freeHead = m_nextFree[index];
            ++m_liveCount;
            return {index, m_generations[index]};
        }

        m_slots.push_back(std::move(value));
        // Fresh slots start at generation 1, never 0: 0 is the null handle.
        m_generations.push_back(1);
        m_nextFree.push_back(FreeListEnd);
        ++m_liveCount;
        return {static_cast<u32>(m_slots.size() - 1), 1};
    }

    [[nodiscard]] bool contains(Handle id) const noexcept { return resolve(id) != nullptr; }

    [[nodiscard]] T* find(Handle id) noexcept { return const_cast<T*>(resolve(id)); }

    [[nodiscard]] const T* find(Handle id) const noexcept { return resolve(id); }

    // Returns false for a handle that was already stale, so a double free is a
    // reportable condition at the call site rather than silently idempotent.
    bool erase(Handle id) noexcept
    {
        if (resolve(id) == nullptr)
            return false;

        m_slots[id.index].reset();
        bumpGeneration(id.index);
        m_nextFree[id.index] = m_freeHead;
        m_freeHead = id.index;
        --m_liveCount;
        return true;
    }

    [[nodiscard]] usize size() const noexcept { return m_liveCount; }
    [[nodiscard]] bool empty() const noexcept { return m_liveCount == 0; }

    // Slot capacity, not live count: what iteration walks.
    [[nodiscard]] usize slotCount() const noexcept { return m_slots.size(); }

    void clear() noexcept
    {
        // Generations are deliberately NOT reset. Handles taken before a clear
        // must not spring back to life when the slots refill.
        for (usize index = 0; index < m_slots.size(); ++index)
        {
            if (m_slots[index].has_value())
            {
                const u32 slot = static_cast<u32>(index);
                m_slots[index].reset();
                bumpGeneration(slot);
                m_nextFree[slot] = m_freeHead;
                m_freeHead = slot;
            }
        }
        m_liveCount = 0;
    }

    // Visits every live slot in ascending index order as `fn(InstanceId, T&)`.
    // Inserting or erasing during a walk is not supported -- collect first.
    template <class Fn>
    void forEach(Fn&& fn)
    {
        for (usize index = 0; index < m_slots.size(); ++index)
        {
            if (m_slots[index].has_value())
                fn(Handle{static_cast<u32>(index), m_generations[index]}, *m_slots[index]);
        }
    }

    template <class Fn>
    void forEach(Fn&& fn) const
    {
        for (usize index = 0; index < m_slots.size(); ++index)
        {
            if (m_slots[index].has_value())
                fn(Handle{static_cast<u32>(index), m_generations[index]}, *m_slots[index]);
        }
    }

private:
    static constexpr u32 FreeListEnd = 0xFFFFFFFFu;

    [[nodiscard]] const T* resolve(Handle id) const noexcept
    {
        if (!id.valid() || id.index >= m_slots.size())
            return nullptr;
        if (m_generations[id.index] != id.generation)
            return nullptr;
        return m_slots[id.index].has_value() ? &*m_slots[id.index] : nullptr;
    }

    // Wrapping skips 0, since generation 0 means "no instance". A slot has to be
    // recycled 2^32 times to get here; the check costs a branch on a path that
    // already touches memory, and the alternative is a handle that silently
    // resolves to the wrong object.
    void bumpGeneration(u32 index) noexcept
    {
        m_generations[index] = (m_generations[index] == FreeListEnd) ? 1u : m_generations[index] + 1u;
    }

    std::vector<std::optional<T>> m_slots;
    std::vector<u32> m_generations;
    // The free list is threaded through the dead slots themselves rather than
    // held in a side vector, so `erase` and `clear` allocate nothing. That is
    // what lets them be honestly `noexcept`: a side vector's `push_back` can
    // throw `bad_alloc`, and a `noexcept` function that throws calls
    // `std::terminate` -- freeing an instance is not a place to discover that.
    // Reuse is still LIFO, which is all the order has to be to stay a pure
    // function of the operation sequence.
    std::vector<u32> m_nextFree;
    u32 m_freeHead = FreeListEnd;
    usize m_liveCount = 0;
};

} // namespace luaug::core
