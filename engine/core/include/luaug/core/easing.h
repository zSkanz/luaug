// The eleven easing curves `TweenService` and `Enum.EasingStyle` name
// (api-design.md §2.1, §2.3).
//
// In `core` rather than in the module that tweens, because two modules ease:
// `script` runs property tweens and `ui` animates a layout, and a second copy
// of these formulas would be two curves under one name.
//
// The formulas are the published ones -- Penner's, which is what every engine
// and design tool means by "quad out" -- and `tests/fixtures/easing.luau` pins
// them as literal numbers written independently from the same formulas. That
// pairing is deliberate: a fixture generated from this file would certify
// whatever this file did, which is the shape of gate M4.5 spent a milestone
// learning to distrust.
#pragma once

#include "luaug/core/types.h"

namespace luaug::core {

// `Enum.EasingStyle`'s values, in the IDL's order.
enum class EasingStyle : u8
{
    Linear = 0,
    Sine = 1,
    Quad = 2,
    Cubic = 3,
    Quart = 4,
    Quint = 5,
    Exponential = 6,
    Circular = 7,
    Back = 8,
    Bounce = 9,
    Elastic = 10,
};

// `Enum.EasingDirection`'s values.
enum class EasingDirection : u8
{
    In = 0,
    Out = 1,
    InOut = 2,
};

// The eased progress for a raw progress.
//
// **Not clamped, and not clamping is the contract.** `Back` and `Elastic`
// deliberately leave [0, 1] -- overshoot is the whole of what they are -- and a
// clamp here would flatten them into slower versions of `Quad`. A property that
// cannot tolerate an overshoot should not be tweened with a style that has one.
//
// `alpha` outside [0, 1] extrapolates rather than saturating, for the same
// reason: the caller asked.
[[nodiscard]] f32 ease(f32 alpha, EasingStyle style, EasingDirection direction) noexcept;

} // namespace luaug::core
