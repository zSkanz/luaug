// Where a thing is, when the thing has no single transform of its own.
//
// **This is one definition, in one place, because it was two.** `GetPivot`,
// `PivotTo` and `GetExtentsSize` grew inside the Luau binding, and everything
// else that needs a pivot -- `Model.Scale` scaling about it, an editor gizmo
// standing on it -- lives below `script` and could not reach them. A second copy
// would be a second answer to "where is the middle of this model", and the last
// time this file's rules were stated twice the comment said "the centre of the
// extents box" while the code averaged part positions, which is a different
// point whenever two parts differ in size.
//
// `scene` is the right floor for it: everything here is a walk over the instance
// tree and its components, and nothing needs a VM.
#pragma once

#include "luaug/core/math.h"
#include "luaug/scene/world.h"

namespace luaug::scene {

// `PivotOffset`, or the identity for an instance that has no `PVComponent` --
// which is every class that is not a `PVInstance`.
[[nodiscard]] core::CFrameD pivotOffsetOf(const World& world, core::InstanceId id) noexcept;

// The axis-aligned box enclosing every part under `id`, in world space. False
// when there is no part under it at all, and then `minimum`/`maximum` are
// untouched.
//
// Shared with `GetExtentsSize`, which is the point: a model's fallback pivot is
// the centre of the box that call reports, rather than a second definition of
// "middle" that happens to agree when every part is the same size.
[[nodiscard]] bool worldExtents(const World& world, core::InstanceId id, core::DVec3& minimum, core::DVec3& maximum);

// The transform a pivot is measured from, before `PivotOffset` is applied.
//
// A `BasePart` and a `Camera` each have one of their own. A `Model` has no
// transform, so it borrows its primary part's when there is one and falls back
// to the centre of its extents box when there is not -- unrotated, because a
// group of parts has no orientation to inherit.
[[nodiscard]] core::CFrameD pivotBase(const World& world, core::InstanceId id);

// Where this instance turns and scales about.
//
// A Model's base already carries its primary part's offset, so applying the
// model's own on top of it would compose two. It does not: `PivotOffset` on a
// Model shifts the model's pivot away from wherever the base put it, which is
// the same sentence as for a part.
[[nodiscard]] core::CFrameD pivotOf(const World& world, core::InstanceId id);

} // namespace luaug::scene
