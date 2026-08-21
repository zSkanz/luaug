// The facts `scene` produces and `script` turns into signal fires
// (api-design.md §3.1, and the seam described in the M2 brief's Decision 3).
//
// This is the whole of the scene↔script contract for events, and its shape is
// the point: every entry is POD. Signal *arguments* can be arbitrary Luau
// values and connections hold Luau functions, so if `scene` owned the signal
// queue it would hold references into the VM -- and L3 would depend on L5
// (architecture.md §2 rule 2), while the ECS would stop being snapshottable.
//
// So the queue is split by what it carries. `scene` enqueues facts that mean
// something in a headless world with no VM at all; `script` consumes them,
// resolves connections, and runs handlers. `Signal.new()` fires, which do carry
// Luau values, live entirely on the `script` side of the same drain.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"
#include "luaug/scene/types.h"

#include <span>
#include <vector>

namespace luaug::scene {

enum class ChangeKind : u8
{
    // `subject` changed the property named by `name`. Only enqueued when the
    // value actually changed (api-design.md §3.1): the signal is a past-tense
    // fact about a change, and an unconditional enqueue would make the
    // 10k-parts benchmark pathological for nothing.
    PropertyChanged,
    AttributeChanged,

    // `subject` gained or lost the child `other`.
    ChildAdded,
    ChildRemoved,

    // `subject` is the ancestor; `other` is the descendant.
    DescendantAdded,
    DescendantRemoving,

    // `subject` moved; `other` is its new parent, invalid when unparented.
    AncestryChanged,

    // `subject` is being destroyed. Its handle still resolves until the end of
    // the drain that processes this (divergence #25).
    Destroying,

    TagAdded,
    TagRemoved,

    // `subject`'s event named by `name` fired, carrying `other` as its single
    // Instance argument -- nil when `other` is invalid.
    //
    // Generic rather than one kind per signal, because the three physics
    // signals M5 adds (`Touched`, `TouchEnded`, `Landed`) all have exactly this
    // shape, and a kind per event would grow this enum with every milestone
    // that has one. The event's name is data here rather than an enumerator for
    // the same reason a property's name is.
    InstanceEvent,

    // The same, carrying NOTHING. `Destroying` is the shape, and M6's
    // `InputAction.Pressed`, `Released` and `StateChanged` are three more of it.
    //
    // A separate kind rather than `InstanceEvent` with an invalid `other`,
    // because those are different arities and not a value: `Landed` with an
    // invalid `other` fires with one nil argument, which is right for a
    // character that landed on nothing and wrong for a button that was pressed.
    InstanceEventNoArgs,
};

// 16 bytes, trivially copyable, and deliberately not a variant: one shape means
// the queue is a flat vector rather than a discriminated allocation per entry,
// and a property storm is the load case this whole design is measured against.
struct Change
{
    ChangeKind kind = ChangeKind::PropertyChanged;
    core::InstanceId subject;
    core::InstanceId other;
    core::NameAtom name;
};

// Append-only within a frame; `drain` hands the whole run to the consumer and
// starts a fresh one. Order is raise order and nothing reorders it -- that
// single total order is what makes a script's own `Fire` and the `ChildAdded`
// caused by its `part.Parent = x` land in the order the script wrote them
// (api-design.md §3.1).
class ChangeQueue
{
public:
    void push(const Change& change) { m_entries.push_back(change); }

    // The consumer must finish with the span before the next `push`, which is
    // the natural shape of a drain: `script` copies what it needs into its own
    // structures as it resolves connections.
    [[nodiscard]] std::span<const Change> take() noexcept
    {
        m_drained.swap(m_entries);
        m_entries.clear();
        return m_drained;
    }

    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
    [[nodiscard]] usize size() const noexcept { return m_entries.size(); }

    void clear() noexcept
    {
        m_entries.clear();
        m_drained.clear();
    }

private:
    std::vector<Change> m_entries;
    // Swapped rather than copied, so a drain costs no allocation and the
    // storage is reused frame after frame.
    std::vector<Change> m_drained;
};

} // namespace luaug::scene
