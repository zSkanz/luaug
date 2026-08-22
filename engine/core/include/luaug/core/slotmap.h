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

#include "luaug/core/id.h"
#include "luaug/core/types.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace luaug::core {

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
        if (m_freeHead != FreeListEnd) {
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
        for (usize index = 0; index < m_slots.size(); ++index) {
            if (m_slots[index].has_value()) {
                const u32 slot = static_cast<u32>(index);
                m_slots[index].reset();
                bumpGeneration(slot);
                m_nextFree[slot] = m_freeHead;
                m_freeHead = slot;
            }
        }
        m_liveCount = 0;
    }

    // Puts the map back into a state captured by copying it, which is what
    // `scene::World::snapshot` does with the instance map (ADR 0016).
    //
    // A plain assignment would get exactly one thing wrong, and it is the thing
    // this function exists for. A restore rolls a slot's generation BACKWARD --
    // deliberately, because that is what makes a handle taken before the copy
    // resolve again, and an editor holding a selection across a Stop depends on
    // it. But every generation the slot passed through in between belonged to
    // some other occupant, and handles naming those are still in the caller's
    // hands. If the ordinary bump handed one of those numbers out a second time,
    // a stale handle would silently start resolving to a NEW object, which is
    // the single failure generations exist to prevent.
    //
    // So the two kinds of slot are treated differently:
    //
    //   * **Occupied in the snapshot** -- its generation comes back exactly,
    //     because a live handle names it and that handle must resolve.
    //   * **Free** (in the snapshot, or created after it) -- its generation goes
    //     ABOVE every value it has ever held. Nothing can be resolving through a
    //     free slot, so there is nothing to preserve, and moving past the range
    //     is what stops it being reissued.
    //
    // The high-water mark is remembered so ordinary recycling after the restore
    // continues above the range too. It costs one `u32` per slot and only in a
    // map that has actually been restored.
    void restoreFrom(const SlotMap& snapshot)
    {
        const usize total = std::max(m_slots.size(), snapshot.m_slots.size());

        // Refreshed from both sides: this map's generations are the highest
        // reached since the snapshot was taken, the snapshot's the highest
        // reached before it, and a ceiling already here the highest of an
        // earlier restore's.
        m_generationCeiling.resize(total, 0);
        for (usize index = 0; index < m_generations.size(); ++index)
            m_generationCeiling[index] = std::max(m_generationCeiling[index], m_generations[index]);
        for (usize index = 0; index < snapshot.m_generations.size(); ++index)
            m_generationCeiling[index] = std::max(m_generationCeiling[index], snapshot.m_generations[index]);

        m_slots.resize(total);
        m_generations.resize(total, 1);
        m_nextFree.resize(total, FreeListEnd);

        for (usize index = 0; index < snapshot.m_slots.size(); ++index) {
            m_slots[index] = snapshot.m_slots[index];
            m_generations[index] = snapshot.m_generations[index];
            m_nextFree[index] = snapshot.m_nextFree[index];
        }
        m_freeHead = snapshot.m_freeHead;
        m_liveCount = snapshot.m_liveCount;

        // Slots the snapshot never saw hold nothing it can put back, so they are
        // freed -- onto the TAIL of the restored free list, so that allocation
        // after a restore reuses what the original run would have reused first
        // and only then reaches ground the original run never touched. Found
        // once rather than per slot, because the list is threaded and walking it
        // per append is quadratic.
        u32 tail = m_freeHead;
        while (tail != FreeListEnd && m_nextFree[tail] != FreeListEnd)
            tail = m_nextFree[tail];
        for (usize index = snapshot.m_slots.size(); index < total; ++index) {
            const u32 slot = static_cast<u32>(index);
            m_slots[index].reset();
            m_nextFree[slot] = FreeListEnd;
            if (tail == FreeListEnd)
                m_freeHead = slot;
            else
                m_nextFree[tail] = slot;
            tail = slot;
        }

        for (usize index = 0; index < total; ++index) {
            if (!m_slots[index].has_value())
                m_generations[index] = nextGeneration(static_cast<u32>(index));
        }
    }

    // Visits every live slot in ascending index order as `fn(InstanceId, T&)`.
    // Inserting or erasing during a walk is not supported -- collect first.
    template <class Fn>
    void forEach(Fn&& fn)
    {
        for (usize index = 0; index < m_slots.size(); ++index) {
            if (m_slots[index].has_value())
                fn(Handle{static_cast<u32>(index), m_generations[index]}, *m_slots[index]);
        }
    }

    template <class Fn>
    void forEach(Fn&& fn) const
    {
        for (usize index = 0; index < m_slots.size(); ++index) {
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

    // The next generation slot `index` may hand out: one past the one it holds,
    // and never one it has already used.
    //
    // Wrapping skips 0, since generation 0 means "no instance". A slot has to be
    // recycled 2^32 times to get here; the check costs a branch on a path that
    // already touches memory, and the alternative is a handle that silently
    // resolves to the wrong object.
    [[nodiscard]] u32 nextGeneration(u32 index) const noexcept
    {
        u32 next = (m_generations[index] == FreeListEnd) ? 1u : m_generations[index] + 1u;
        // Empty unless `restoreFrom` has run, so a map that is never restored
        // pays one bounds check and nothing else.
        if (index < m_generationCeiling.size() && next <= m_generationCeiling[index])
            next = (m_generationCeiling[index] == FreeListEnd) ? 1u : m_generationCeiling[index] + 1u;
        return next;
    }

    void bumpGeneration(u32 index) noexcept { m_generations[index] = nextGeneration(index); }

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
    // The highest generation each slot has ever held, and empty until the first
    // `restoreFrom`. It is the only reason a restore can roll a generation back
    // without the number becoming reusable; see that function for why that
    // matters. Not sized eagerly, because most maps are never restored and the
    // vector would otherwise be a third of the map's per-slot cost for nothing.
    std::vector<u32> m_generationCeiling;
    u32 m_freeHead = FreeListEnd;
    usize m_liveCount = 0;
};

} // namespace luaug::core
