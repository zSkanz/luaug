#include "luaug/core/easing.h"

#include <cmath>

namespace luaug::core {
namespace {

constexpr f32 Pi = 3.14159265358979323846f;

// The classic overshoot constant. 1.70158 is not a taste: it is the value that
// makes `Back` overshoot by exactly 10%, and it is the number every other
// implementation of this curve uses -- which is the point of naming a curve
// after a published formula.
constexpr f32 BackOvershoot = 1.70158f;

// Penner's elastic parameters: amplitude 1, period 0.3, and the phase shift
// that follows from them.
constexpr f32 ElasticPeriod = 0.3f;
constexpr f32 ElasticShift = ElasticPeriod / 4.0f;

// `Bounce` is defined by its OUT half and the other two are derived from it,
// which is the reverse of every other style here. That is how the formula is
// published, and deriving it the other way round produces a curve that bounces
// before it lands.
[[nodiscard]] f32 bounceOut(f32 t) noexcept
{
    constexpr f32 n = 7.5625f;
    constexpr f32 d = 2.75f;
    if (t < 1.0f / d)
        return n * t * t;
    if (t < 2.0f / d) {
        t -= 1.5f / d;
        return n * t * t + 0.75f;
    }
    if (t < 2.5f / d) {
        t -= 2.25f / d;
        return n * t * t + 0.9375f;
    }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

// Every style's IN half. `Out` is this mirrored and `InOut` is the two halves
// glued, so the eleven curves are eleven expressions rather than thirty-three.
[[nodiscard]] f32 easeIn(f32 t, EasingStyle style) noexcept
{
    switch (style) {
    case EasingStyle::Linear:
        return t;
    case EasingStyle::Sine:
        return 1.0f - std::cos(t * Pi * 0.5f);
    case EasingStyle::Quad:
        return t * t;
    case EasingStyle::Cubic:
        return t * t * t;
    case EasingStyle::Quart:
        return t * t * t * t;
    case EasingStyle::Quint:
        return t * t * t * t * t;
    case EasingStyle::Exponential:
        // Exactly 0 at 0, rather than 2^-10 -- which is 0.00098 and would leave
        // a property visibly off its start for the first frame.
        return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
    case EasingStyle::Circular:
        return 1.0f - std::sqrt(std::fmax(0.0f, 1.0f - t * t));
    case EasingStyle::Back:
        return t * t * ((BackOvershoot + 1.0f) * t - BackOvershoot);
    case EasingStyle::Bounce:
        return 1.0f - bounceOut(1.0f - t);
    case EasingStyle::Elastic:
        if (t == 0.0f)
            return 0.0f;
        if (t == 1.0f)
            return 1.0f;
        return -std::pow(2.0f, 10.0f * (t - 1.0f)) * std::sin((t - 1.0f - ElasticShift) * 2.0f * Pi / ElasticPeriod);
    }
    return t;
}

} // namespace

f32 ease(f32 alpha, EasingStyle style, EasingDirection direction) noexcept
{
    switch (direction) {
    case EasingDirection::In:
        return easeIn(alpha, style);
    case EasingDirection::Out:
        return 1.0f - easeIn(1.0f - alpha, style);
    case EasingDirection::InOut:
        // The IN curve over the first half and its mirror over the second, each
        // squeezed into half the range. Written as two expressions rather than
        // as a call to the Out branch, because the second half has to be the
        // mirror of THIS style's In and not of a generic ease-out.
        if (alpha < 0.5f)
            return easeIn(alpha * 2.0f, style) * 0.5f;
        return 1.0f - easeIn(2.0f - alpha * 2.0f, style) * 0.5f;
    }
    return alpha;
}

} // namespace luaug::core
