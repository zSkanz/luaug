#include "luaug/core/random.h"

#include <algorithm>
#include <cmath>

namespace luaug::core {
namespace {

// PCG-XSH-RR 64/32, verbatim from the reference implementation. Every constant
// here is part of the published algorithm rather than a tuning choice: changing
// one does not produce "different random numbers", it produces a generator whose
// statistical properties nobody has tested and whose sequence no other
// implementation can reproduce.
constexpr u64 kMultiplier = 6364136223846793005ull;

// 2^53, the largest integer for which every smaller integer is exactly
// representable as an f64 -- so a draw of 53 bits divides down to a double with
// no rounding and no gaps.
constexpr f64 kTwo53 = 9007199254740992.0;

constexpr f64 kTwoPi = 6.283185307179586476925286766559;

// The permutation half of PCG: xorshift the high bits down, then rotate by an
// amount taken from the *top* bits of the same word. The data-dependent rotate
// is what breaks the low-bit regularity an LCG on its own has.
[[nodiscard]] u32 permute(u64 state) noexcept
{
    const u32 xorshifted = static_cast<u32>(((state >> 18u) ^ state) >> 27u);
    const u32 rotation = static_cast<u32>(state >> 59u);
    // `(32 - rotation) & 31` rather than `32 - rotation`: a rotation of 0 would
    // otherwise shift a 32-bit value by 32, which is undefined behaviour, and
    // one draw in 32 takes that branch.
    return (xorshifted >> rotation) | (xorshifted << ((32u - rotation) & 31u));
}

} // namespace

Pcg32::Pcg32(u64 seed, u64 stream) noexcept
{
    // PCG's own seeding procedure, not an approximation of it: state to zero,
    // step, add the seed, step. The two steps are what stop adjacent seeds from
    // producing sequences that start out visibly correlated -- the failure mode
    // of naively assigning the seed to the state, which is what makes
    // "seed with the entity index" quietly useless.
    m_state = 0;
    // The low bit is forced rather than checked: an even increment halves the
    // period and costs the stream its independence, and `stream << 1` gives
    // every one of the 2^63 streams a distinct odd increment.
    m_increment = (stream << 1u) | 1u;
    (void)nextU32();
    m_state += seed;
    (void)nextU32();
}

u32 Pcg32::nextU32() noexcept
{
    // The output is a permutation of the state the generator is leaving, not the
    // one it is moving to. Permuting the new state instead is a different
    // generator that looks equally random and matches no reference vector.
    const u64 previous = m_state;
    m_state = previous * kMultiplier + m_increment;
    return permute(previous);
}

f64 Pcg32::nextDouble() noexcept
{
    // Two draws, 27 bits then 26, because a double's mantissa is wider than one
    // draw: taking a single u32 and dividing by 2^32 leaves 2^32 reachable
    // values out of the 2^53 the type can express, which a distribution test
    // notices long before a player does.
    //
    // The two draws are sequenced by separate statements on purpose. Written as
    // one expression the order of the operands is unspecified, and this
    // generator's entire contract is that the same calls consume the stream in
    // the same order on every compiler.
    const u64 high = static_cast<u64>(nextU32()) >> 5u;
    const u64 low = static_cast<u64>(nextU32()) >> 6u;
    return static_cast<f64>((high << 26u) | low) * (1.0 / kTwo53);
}

f64 Pcg32::nextDouble(f64 min, f64 max) noexcept
{
    return min + (max - min) * nextDouble();
}

i32 Pcg32::nextInt(i32 min, i32 max) noexcept
{
    // A range with one outcome consumes no randomness. That is a contract, not
    // an optimisation: it means a call whose bounds collapse cannot shift the
    // stream position for everything downstream of it. `max < min` is a caller
    // bug and lands here too, which keeps the function total.
    if (max <= min)
        return min;

    // Both bounds move into unsigned before anything is subtracted. `max - min`
    // overflows a signed 32-bit int as soon as the range is wider than half the
    // type, and signed overflow is undefined -- not something a determinism
    // guarantee can be built on top of. The reverse bias at the end is exact:
    // C++20 defines the unsigned-to-signed conversion as the two's-complement
    // wrap this relies on.
    const u32 base = static_cast<u32>(min);
    const u32 span = static_cast<u32>(max) - base;

    // The whole 32-bit range: every draw is in bounds, so there is nothing to
    // reject and nothing to fold. Also the one case `span + 1` cannot express.
    if (span == 0xFFFFFFFFu)
        return static_cast<i32>(nextU32());

    const u32 count = span + 1u;
    // Values below this threshold are the short tail that folding by `% count`
    // would over-represent -- there are 2^32 mod count of them. Rejecting them
    // makes the result exactly uniform instead of uniform-to-within-2^-30, which
    // is invisible in play and very visible in the distribution test that gates
    // this file.
    const u32 threshold = static_cast<u32>(0x100000000ull % count);

    u32 draw = nextU32();
    while (draw < threshold)
        draw = nextU32();

    return static_cast<i32>(base + (draw % count));
}

Vec3 Pcg32::nextUnitVector() noexcept
{
    // Archimedes' hat-box: a sphere's area between two parallel planes depends
    // only on the distance between them, so a uniform z and a uniform azimuth
    // are already uniform on the sphere -- no rejection, two draws, always.
    //
    // The alternative that suggests itself, normalising a random point in a
    // cube, is not uniform: it over-weights the eight corner directions by up to
    // sqrt(3), which reads as "the sparks keep flying diagonally".
    //
    // Separate statements again, for the reason nextDouble() gives.
    const f64 z = nextDouble(-1.0, 1.0);
    const f64 azimuth = nextDouble(0.0, kTwoPi);

    // Clamped because a rounded 1 - z*z can land a hair below zero, and a NaN
    // that reached a transform from here would be traced back to the transform.
    const f64 radius = std::sqrt(std::max(0.0, 1.0 - z * z));

    return Vec3{
        static_cast<f32>(radius * std::cos(azimuth)),
        static_cast<f32>(radius * std::sin(azimuth)),
        static_cast<f32>(z),
    };
}

void Pcg32::setState(u64 state, u64 increment) noexcept
{
    m_state = state;
    // Oddness is re-imposed rather than trusted. A restored snapshot is exactly
    // where a hand-edited or corrupted increment arrives, and an even one does
    // not fail loudly -- it silently halves the period.
    m_increment = increment | 1ull;
}

} // namespace luaug::core
