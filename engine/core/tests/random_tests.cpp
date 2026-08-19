#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "luaug/core/math.h"
#include "luaug/core/random.h"

using luaug::core::f32;
using luaug::core::f64;
using luaug::core::i32;
using luaug::core::Pcg32;
using luaug::core::u32;
using luaug::core::u64;
using luaug::core::usize;
using luaug::core::Vec3;

namespace
{

// One place to change the shape of the "same seed, same numbers" evidence: every
// determinism check below draws through this, so adding a call to the mix covers
// all of them at once.
//
// Floating results go in as bit patterns, not values. Two doubles that differ in
// the last mantissa bit are not "close enough" for a replay, and comparing them
// as numbers with a tolerance would let exactly that divergence through.
std::vector<u64> drawMixedSequence(Pcg32& rng, int rounds)
{
    std::vector<u64> out;
    out.reserve(static_cast<usize>(rounds) * 5u);

    for (int round = 0; round < rounds; ++round)
    {
        out.push_back(rng.nextU32());

        const f64 sample = rng.nextDouble();
        u64 sampleBits = 0;
        std::memcpy(&sampleBits, &sample, sizeof(sampleBits));
        out.push_back(sampleBits);

        out.push_back(static_cast<u64>(static_cast<u32>(rng.nextInt(-1000, 1000))));

        const Vec3 direction = rng.nextUnitVector();
        u32 xBits = 0;
        u32 zBits = 0;
        std::memcpy(&xBits, &direction.x, sizeof(xBits));
        std::memcpy(&zBits, &direction.z, sizeof(zBits));
        out.push_back(xBits);
        out.push_back(zBits);
    }

    return out;
}

// The bands are checked after the loop rather than inside it: a per-draw assert
// costs more than the generator does, and the extremes carry the same
// information a failing draw would.
void checkFlat(const int* counts, usize buckets, int expected)
{
    // Five percent is roughly five standard deviations at the sample sizes used
    // here, so the band is wide enough never to trip on honest noise. It is a
    // fixed seed and a fixed count either way, so this test is deterministic:
    // it always passes or always fails, never sometimes.
    const int slack = expected / 20;
    for (usize bucket = 0; bucket < buckets; ++bucket)
    {
        CHECK(counts[bucket] > expected - slack);
        CHECK(counts[bucket] < expected + slack);
    }
}

} // namespace

TEST_CASE("the generator is PCG-XSH-RR 64/32, not a variant of it")
{
    // The reference vector from PCG's own demo for seed 42, stream 54. This is
    // what makes the sequence a *known* quantity rather than merely a repeatable
    // one: a transcription slip in the multiplier, the shift widths or the
    // rotate produces a generator that still looks random, still reproduces on
    // both compilers, and matches nothing anyone else can check against.
    Pcg32 rng(42, 54);

    CHECK(rng.nextU32() == 0xa15c02b7u);
    CHECK(rng.nextU32() == 0x7b47f409u);
    CHECK(rng.nextU32() == 0xba1d3330u);
    CHECK(rng.nextU32() == 0x83d2f293u);
    CHECK(rng.nextU32() == 0xbfa4784bu);
    CHECK(rng.nextU32() == 0xcbed606eu);
}

TEST_CASE("the same seed and stream produce the same sequence")
{
    // The headline guarantee (R10, ADR 0025). Everything the replay harness will
    // claim about a recorded run reduces to this.
    Pcg32 first(0xDEADBEEFull, 7);
    Pcg32 second(0xDEADBEEFull, 7);

    CHECK(drawMixedSequence(first, 200) == drawMixedSequence(second, 200));
}

TEST_CASE("different streams from one seed do not correlate")
{
    // What lets each system take its own randomness from one world seed instead
    // of taking turns from a shared generator -- and taking turns is how a
    // change in one system silently changes another system's numbers.
    Pcg32 streamOne(1234, 1);
    Pcg32 streamTwo(1234, 2);

    CHECK(drawMixedSequence(streamOne, 50) != drawMixedSequence(streamTwo, 50));
}

TEST_CASE("different seeds on one stream diverge immediately")
{
    // PCG's two-step seeding is what buys this. Assigning the seed straight to
    // the state gives adjacent seeds sequences that start out visibly alike,
    // which is what makes "seed it with the entity index" a trap.
    Pcg32 first(1000, 1);
    Pcg32 second(1001, 1);

    CHECK(first.nextU32() != second.nextU32());
    CHECK(drawMixedSequence(first, 50) != drawMixedSequence(second, 50));
}

TEST_CASE("the stream increment is always odd")
{
    // An even increment costs PCG its full period, and nothing about the output
    // looks different when it happens.
    CHECK((Pcg32(1, 0).increment() & 1ull) == 1ull);
    CHECK((Pcg32(1, 1).increment() & 1ull) == 1ull);

    const u64 expected = (12345ull << 1u) | 1ull;
    CHECK(Pcg32(1, 12345).increment() == expected);
}

TEST_CASE("state and increment round-trip, so a clone continues identically")
{
    // Random:Clone in api-design.md §2.3 and snapshot/restore for the replay
    // harness are the same operation seen from two sides.
    Pcg32 original(99, 5);
    for (int step = 0; step < 17; ++step)
        (void)original.nextU32();

    Pcg32 clone(0);
    clone.setState(original.state(), original.increment());

    CHECK(clone.state() == original.state());
    CHECK(clone.increment() == original.increment());
    CHECK(drawMixedSequence(original, 100) == drawMixedSequence(clone, 100));
}

TEST_CASE("setState re-imposes an odd increment")
{
    // A restored snapshot is exactly where a hand-edited or corrupted increment
    // arrives, and an even one does not fail loudly.
    Pcg32 rng(1);
    rng.setState(0x0123456789ABCDEFull, 4);

    CHECK(rng.state() == 0x0123456789ABCDEFull);
    CHECK(rng.increment() == 5ull);
}

TEST_CASE("nextInt stays inside its inclusive bounds and reaches both of them")
{
    Pcg32 rng(2024, 1);
    i32 lowest = std::numeric_limits<i32>::max();
    i32 highest = std::numeric_limits<i32>::min();

    for (int draw = 0; draw < 20000; ++draw)
    {
        const i32 value = rng.nextInt(-3, 4);
        lowest = value < lowest ? value : lowest;
        highest = value > highest ? value : highest;
    }

    // Inclusive at both ends. An off-by-one here is the classic silent bug: a
    // range that never yields its top value looks perfectly fine in play.
    CHECK(lowest == -3);
    CHECK(highest == 4);
}

TEST_CASE("nextInt handles a single-value range and a reversed one")
{
    Pcg32 rng(7, 1);
    Pcg32 shadow(7, 1);

    CHECK(rng.nextInt(3, 3) == 3);
    CHECK(rng.nextInt(-8, -8) == -8);
    // A reversed range is a caller bug; it yields the low bound rather than
    // looping forever looking for a value that cannot exist.
    CHECK(rng.nextInt(10, 2) == 10);

    // None of those had a choice to make, so none of them consumed the stream.
    // That is a contract, not an optimisation: a call whose bounds collapse must
    // not shift the numbers everything downstream of it gets.
    CHECK(rng.nextU32() == shadow.nextU32());
}

TEST_CASE("nextInt spans the full 32-bit range without overflowing")
{
    // max - min is 2^32 - 1 here, which does not fit the signed type the bounds
    // are given in -- computing the span signed would be undefined behaviour --
    // and a count one wider than u32 can express is the case a rejection loop
    // gets wrong.
    constexpr i32 low = std::numeric_limits<i32>::min();
    constexpr i32 high = std::numeric_limits<i32>::max();

    Pcg32 rng(555, 1);
    bool sawNegative = false;
    bool sawPositive = false;

    for (int draw = 0; draw < 4000; ++draw)
    {
        const i32 value = rng.nextInt(low, high);
        sawNegative = sawNegative || value < 0;
        sawPositive = sawPositive || value > 0;
    }

    CHECK(sawNegative);
    CHECK(sawPositive);
}

TEST_CASE("nextInt is uniform across its buckets")
{
    constexpr usize kBuckets = 10;
    constexpr int kDraws = 200000;

    Pcg32 rng(31337, 1);
    std::array<int, kBuckets> counts{};
    i32 lowest = std::numeric_limits<i32>::max();
    i32 highest = std::numeric_limits<i32>::min();

    for (int draw = 0; draw < kDraws; ++draw)
    {
        const i32 value = rng.nextInt(0, static_cast<i32>(kBuckets) - 1);
        lowest = value < lowest ? value : lowest;
        highest = value > highest ? value : highest;
        ++counts[static_cast<usize>(value) % kBuckets];
    }

    CHECK(lowest == 0);
    CHECK(highest == static_cast<i32>(kBuckets) - 1);
    checkFlat(counts.data(), kBuckets, kDraws / static_cast<int>(kBuckets));
}

TEST_CASE("nextDouble stays in [0, 1) and covers it evenly")
{
    constexpr usize kBuckets = 10;
    constexpr int kDraws = 200000;

    Pcg32 rng(8675309, 1);
    std::array<int, kBuckets> counts{};
    f64 lowest = 2.0;
    f64 highest = -1.0;

    for (int draw = 0; draw < kDraws; ++draw)
    {
        const f64 value = rng.nextDouble();
        lowest = value < lowest ? value : lowest;
        highest = value > highest ? value : highest;

        const auto bucket = static_cast<usize>(value * static_cast<f64>(kBuckets));
        ++counts[bucket < kBuckets ? bucket : kBuckets - 1];
    }

    CHECK(lowest >= 0.0);
    // Half-open. A closed [0, 1] would let `array[int(u * size)]` index one past
    // the end, on one draw in a few billion.
    CHECK(highest < 1.0);
    CHECK(lowest < 0.001);
    CHECK(highest > 0.999);
    checkFlat(counts.data(), kBuckets, kDraws / static_cast<int>(kBuckets));
}

TEST_CASE("nextDouble draws 53 bits, not 32")
{
    // A generator dividing a single u32 by 2^32 would reach only 2^32 of the
    // 2^53 values a double can express, so every draw would be an exact multiple
    // of 2^-32. Catching that needs one bit below that granularity to exist.
    Pcg32 rng(4242, 1);
    bool sawSubU32Precision = false;

    for (int draw = 0; draw < 1000 && !sawSubU32Precision; ++draw)
    {
        const f64 scaled = rng.nextDouble() * 4294967296.0; // 2^32
        sawSubU32Precision = scaled != std::floor(scaled);
    }

    CHECK(sawSubU32Precision);
}

TEST_CASE("nextDouble with bounds stays inside them")
{
    Pcg32 rng(11, 1);
    f64 lowest = 100.0;
    f64 highest = -100.0;

    for (int draw = 0; draw < 50000; ++draw)
    {
        const f64 value = rng.nextDouble(-2.5, 7.5);
        lowest = value < lowest ? value : lowest;
        highest = value > highest ? value : highest;
    }

    CHECK(lowest >= -2.5);
    CHECK(highest < 7.5);
    // Both ends get near-approached, so a draw stuck in a sub-range fails rather
    // than passing the bounds check trivially.
    CHECK(lowest < -2.49);
    CHECK(highest > 7.49);
}

TEST_CASE("nextUnitVector returns unit-length vectors")
{
    Pcg32 rng(1, 1);
    f32 worst = 0.0f;

    for (int draw = 0; draw < 50000; ++draw)
    {
        const f32 deviation = std::fabs(length(rng.nextUnitVector()) - 1.0f);
        worst = deviation > worst ? deviation : worst;
    }

    CHECK(worst <= 1e-5f);
}

TEST_CASE("nextUnitVector is uniform on the sphere, not on the cube")
{
    // The distribution that separates the two constructions. Uniform on the
    // sphere means equal area per band of latitude, so a histogram of z is flat.
    // Normalising a random point in a cube is not flat: it piles up around the
    // eight corner directions at |z| near 0.577, which this catches and a
    // unit-length check never would.
    constexpr usize kBands = 10;
    constexpr int kDraws = 200000;

    Pcg32 rng(90210, 1);
    std::array<int, kBands> zBands{};

    for (int draw = 0; draw < kDraws; ++draw)
    {
        const f64 z = static_cast<f64>(rng.nextUnitVector().z);
        const auto band = static_cast<usize>((z + 1.0) * 0.5 * static_cast<f64>(kBands));
        ++zBands[band < kBands ? band : kBands - 1];
    }

    checkFlat(zBands.data(), kBands, kDraws / static_cast<int>(kBands));
}

TEST_CASE("nextUnitVector spreads over the azimuth too")
{
    // A flat z histogram alone would still pass for a generator that always
    // pointed along +X at a given latitude, so the other angle gets its own.
    constexpr usize kSectors = 8;
    constexpr int kDraws = 160000;
    constexpr f64 kPi = 3.14159265358979323846;

    Pcg32 rng(1618, 1);
    std::array<int, kSectors> sectors{};

    for (int draw = 0; draw < kDraws; ++draw)
    {
        const Vec3 direction = rng.nextUnitVector();
        // atan2 covers [-pi, pi]; remapped to [0, 1).
        const f64 turn =
            (std::atan2(static_cast<f64>(direction.y), static_cast<f64>(direction.x)) + kPi) / (2.0 * kPi);
        const auto sector = static_cast<usize>(turn * static_cast<f64>(kSectors));
        ++sectors[sector < kSectors ? sector : kSectors - 1];
    }

    checkFlat(sectors.data(), kSectors, kDraws / static_cast<int>(kSectors));
}
