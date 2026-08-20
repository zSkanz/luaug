// `nearly` -- comparing two f32s in f32.
//
// It lives here because it has now been written three times in three modules,
// and the reason is always the same: `doctest::Approx` takes a `double`, so
// `CHECK(someFloat == doctest::Approx(other))` promotes -- and `-Wdouble-
// promotion -Werror` is on for Clang, so the line builds on Windows and fails
// the Linux tier. That is a slow way to find out, and copying the workaround
// into each test file made it a lesson every module had to learn separately.
//
// Comparing in f32 throughout also puts "relative to what" in the expression
// rather than in a comment beside it.
#pragma once

#include "luaug/core/types.h"

namespace luaug::testing
{

[[nodiscard]] inline bool nearly(core::f32 value, core::f32 expected, core::f32 tolerance = 1e-5f) noexcept
{
    const core::f32 difference = value > expected ? value - expected : expected - value;
    return difference <= tolerance;
}

} // namespace luaug::testing
