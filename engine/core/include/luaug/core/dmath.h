#pragma once

// **The engine's own transcendentals** (ADR 0083): the same bits on every
// compiler, operating system and architecture.
//
// Each platform's C runtime rounds `sin`, `exp` and the rest in its own way, and
// the last bit is enough: a world hash checkpointed every 150 ticks carries it
// from one `CFrame.fromEuler` to the whole simulation (ADR 0074). These are
// written out -- argument reduction, then a fixed polynomial -- in nothing but
// IEEE double addition, subtraction, multiplication, division and square root,
// each correctly rounded by the standard on every platform. The translation unit
// is compiled with contraction off, so no compiler can fuse a multiply and an
// add into one differently rounded step.
//
// Accuracy is about one unit in the last place, which is what the runtimes
// promise too. What these add is that the answer is the SAME everywhere, and
// that is what the simulation needs.
//
// Everything on the simulation's path -- `core` maths, `CFrame`, easing, the
// seeded random, and Luau's `math` library -- calls these and not `std::`.

namespace luaug::core::dmath {

[[nodiscard]] double sin(double x) noexcept;
[[nodiscard]] double cos(double x) noexcept;
[[nodiscard]] double tan(double x) noexcept;
[[nodiscard]] double asin(double x) noexcept;
[[nodiscard]] double acos(double x) noexcept;
[[nodiscard]] double atan(double x) noexcept;
[[nodiscard]] double atan2(double y, double x) noexcept;

[[nodiscard]] double exp(double x) noexcept;
[[nodiscard]] double log(double x) noexcept;
[[nodiscard]] double log2(double x) noexcept;
[[nodiscard]] double log10(double x) noexcept;
[[nodiscard]] double pow(double x, double y) noexcept;

[[nodiscard]] double sinh(double x) noexcept;
[[nodiscard]] double cosh(double x) noexcept;
[[nodiscard]] double tanh(double x) noexcept;

// Both at once, for the rotation builders that need the pair: one reduction.
void sincos(double x, double& sine, double& cosine) noexcept;

// The single-precision spellings the math types use, through the double ones:
// a float argument is exact as a double, and the answer is rounded once.
[[nodiscard]] inline float sin(float x) noexcept
{
    return static_cast<float>(sin(static_cast<double>(x)));
}
[[nodiscard]] inline float cos(float x) noexcept
{
    return static_cast<float>(cos(static_cast<double>(x)));
}
[[nodiscard]] inline float tan(float x) noexcept
{
    return static_cast<float>(tan(static_cast<double>(x)));
}
[[nodiscard]] inline float asin(float x) noexcept
{
    return static_cast<float>(asin(static_cast<double>(x)));
}
[[nodiscard]] inline float acos(float x) noexcept
{
    return static_cast<float>(acos(static_cast<double>(x)));
}
[[nodiscard]] inline float atan(float x) noexcept
{
    return static_cast<float>(atan(static_cast<double>(x)));
}
[[nodiscard]] inline float atan2(float y, float x) noexcept
{
    return static_cast<float>(atan2(static_cast<double>(y), static_cast<double>(x)));
}
[[nodiscard]] inline float exp(float x) noexcept
{
    return static_cast<float>(exp(static_cast<double>(x)));
}
[[nodiscard]] inline float log(float x) noexcept
{
    return static_cast<float>(log(static_cast<double>(x)));
}
[[nodiscard]] inline float pow(float x, float y) noexcept
{
    return static_cast<float>(pow(static_cast<double>(x), static_cast<double>(y)));
}

} // namespace luaug::core::dmath

// `dmath::pow`, under the name the patched Luau VM and compiler call for `^`
// (`third_party/patches/luau`). Defined in `dmath.cpp`.
double luaug_dpow(double a, double b);
