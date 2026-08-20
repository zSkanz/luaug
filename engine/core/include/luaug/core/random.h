// Deterministic random streams (architecture.md §2, R10, ADR 0025).
//
// PCG32: small state, no global state, and -- the property that actually
// matters here -- identical output for identical seeds on every platform,
// because every step is defined in terms of exact 64-bit integer arithmetic.
// `std::mt19937` would also be reproducible, but `std::uniform_int_distribution`
// is not: the standard leaves its algorithm to the implementation, so the same
// seed gives different numbers on MSVC and libc++. That is why the bounded
// draws below are written out rather than delegated.
//
// Simulation code must never construct one of these from a clock. Seeds come
// from the World's seed, which is recorded in the replay.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

namespace luaug::core {

class Pcg32
{
public:
    // `stream` selects one of 2^63 independent sequences. Two generators with
    // the same seed and different streams do not correlate, which is how
    // separate systems get their own randomness from one world seed without
    // taking turns from a shared generator -- and taking turns from a shared
    // generator is exactly how a change in one system silently changes another
    // system's numbers.
    explicit Pcg32(u64 seed, u64 stream = 1) noexcept;

    [[nodiscard]] u32 nextU32() noexcept;

    // [0, 1). 53 bits of mantissa, so the whole double range is reachable.
    [[nodiscard]] f64 nextDouble() noexcept;

    // [min, max), matching the half-open convention of every other range here.
    [[nodiscard]] f64 nextDouble(f64 min, f64 max) noexcept;

    // [min, max] **inclusive**, matching `Random:NextInteger` in
    // api-design.md §2.3. Rejection-sampled, so the distribution is exactly
    // uniform rather than modulo-biased -- the bias is invisible in play and
    // very visible in a statistical test, and a gate that fails only sometimes
    // is worse than one that fails.
    [[nodiscard]] i32 nextInt(i32 min, i32 max) noexcept;

    // Uniform on the sphere, not on the cube: sampled by inverting the
    // cosine-of-latitude distribution, because normalising a random point in a
    // cube clusters toward the corners.
    [[nodiscard]] Vec3 nextUnitVector() noexcept;

    // The full generator state, for snapshot/restore and for `Random:Clone`.
    // Two generators with the same state produce the same sequence: that is the
    // definition of the clone contract in api-design.md §2.3.
    [[nodiscard]] u64 state() const noexcept { return m_state; }
    [[nodiscard]] u64 increment() const noexcept { return m_increment; }
    void setState(u64 state, u64 increment) noexcept;

private:
    u64 m_state = 0;
    // Always odd -- PCG's period argument depends on it, and the constructor
    // enforces it rather than trusting callers.
    u64 m_increment = 1;
};

} // namespace luaug::core
