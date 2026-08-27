// Reading a world's replicated state, and comparing it with what a peer holds
// (ADR 0069).
//
// **This is the half that has to be right before anything is sent**, and it is
// deliberately free of transports, peers and sockets: everything here is a
// function of a world and a baseline, which is what lets the whole of it be
// tested in a process with no network in it.
#pragma once

#include "luaug/core/id.h"
#include "luaug/replication/field.h"
#include "luaug/replication/types.h"

#include <span>
#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::replication {

namespace generated {
struct ClassDesc;
struct FieldDesc;
} // namespace generated

// The fields one instance replicates: the common set, then its class's, in
// schema order.
//
// **A flat vector and not a map.** It is indexed by position, the position is
// the schema's own order, and both ends compute it from the same generated
// table -- so a peer and an authority cannot disagree about what index three
// means without disagreeing about the protocol version first.
using FieldSet = std::vector<FieldValue>;

// Which class's schema applies to an instance, or null when it replicates
// nothing.
//
// **Walks up the hierarchy**, because `Part` and `MeshPart` have no schema of
// their own and inherit `BasePart`'s. That is the same rule `wirecheck` uses to
// decide a class is "decided", and having the two agree is not a coincidence to
// be maintained -- it is why the checker walks rather than demanding a row per
// leaf.
[[nodiscard]] const generated::ClassDesc* schemaFor(const scene::World& world, core::InstanceId id);

// How many fields an instance of this class carries, common set included.
[[nodiscard]] core::usize fieldCount(const generated::ClassDesc& desc);

// Reads every field of `id` into `out`, sized to `fieldCount`.
//
// Returns false when the instance is not replicable -- no schema, or gone.
// **Never partially fills**: a half-read field set diffed against a baseline
// would report the unread half as changed, every tick, for ever.
[[nodiscard]] bool extractFields(const scene::World& world, core::InstanceId id, const generated::ClassDesc& desc,
                                 FieldSet& out);

// What one instance's diff looks like on the wire.
struct FieldDelta
{
    // The field's PERMANENT id from the schema, not its index. An index is a
    // fact about this build's table; an id is a fact about the protocol.
    core::u16 id = 0;
    FieldValue value;
};

// Every field of `current` that differs from `baseline`.
//
// **A size mismatch means every field changed**, which is the honest answer: it
// happens when a peer's baseline was taken under a different class -- an
// instance replaced by another of the same id -- and sending everything is what
// makes the replica correct rather than subtly wrong.
void diffFields(const generated::ClassDesc& desc, std::span<const FieldValue> baseline,
                std::span<const FieldValue> current, std::vector<FieldDelta>& out);

// The field descriptor at a flat index, common set first. Null past the end.
[[nodiscard]] const generated::FieldDesc* fieldAt(const generated::ClassDesc& desc, core::usize index);

// The id a field travels under, which is NOT `FieldDesc::id`.
//
// **The schema numbers the common fields from 1 and each class's fields from 1
// independently**, so `Name` (common id 1) and `CFrame` (`BasePart` id 1) are
// the same number. The top bit says which half, which makes the wire id unique
// within a class while staying a permanent fact about the protocol rather than
// an index into this build's table.
[[nodiscard]] core::u16 wireIdAt(const generated::ClassDesc& desc, core::usize index);

// Applies one field to an instance. Returns false when the id is not one this
// class has -- which is what a peer speaking a newer protocol looks like, and is
// a refusal rather than a guess.
[[nodiscard]] bool applyField(scene::World& world, core::InstanceId id, const generated::ClassDesc& desc,
                              const FieldDelta& delta);

} // namespace luaug::replication
