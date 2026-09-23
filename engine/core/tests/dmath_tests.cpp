// The engine's own transcendentals (ADR 0083).
//
// **The golden table is the level-C test.** Each row is the exact bit pattern
// the function produced when the table was written, on Windows under MSVC; the
// Linux tier compiles the same file with Clang against glibc, and macOS with
// Apple Clang on ARM, and every one of them must reproduce every bit. A runtime
// `std::sin` would fail this on the second tier, which is the whole reason the
// functions exist.
//
// The accuracy cases say the other half: these are not merely consistent, they
// are right, to within a few units in the last place of each platform's own.
#include "luaug/core/dmath.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <random>

namespace dm = luaug::core::dmath;

namespace {

[[nodiscard]] std::uint64_t bitsOf(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// How far apart two doubles are, in units in the last place of the second.
[[nodiscard]] double ulpsApart(double value, double reference)
{
    if (value == reference)
        return 0.0;
    const double ulp =
        std::nextafter(std::fabs(reference), std::numeric_limits<double>::infinity()) - std::fabs(reference);
    return std::fabs(value - reference) / ulp;
}

struct Golden
{
    const char* what;
    std::uint64_t bits;
    double (*compute)();
};

} // namespace

TEST_CASE("every platform computes the same bits")
{
    static const Golden Table[] = {
        {"sin(1)", 0x3FEAED548F090CEEull, [] { return dm::sin(1.0); }},
        {"cos(1)", 0x3FE14A280FB5068Cull, [] { return dm::cos(1.0); }},
        {"tan(1)", 0x3FF8EB245CBEE3A5ull, [] { return dm::tan(1.0); }},
        {"sin(100000)", 0x3FA24DAA9C527E96ull, [] { return dm::sin(100000.0); }},
        {"cos(-12345.678)", 0x3FE6B94C3BBE24B8ull, [] { return dm::cos(-12345.678); }},
        {"asin(0.5)", 0x3FE0C152382D7366ull, [] { return dm::asin(0.5); }},
        {"acos(0.3)", 0x3FF441F5ECBEEF58ull, [] { return dm::acos(0.3); }},
        {"atan(2)", 0x3FF1B6E192EBBE44ull, [] { return dm::atan(2.0); }},
        {"atan2(1, -2)", 0x40056C6E7397F5AEull, [] { return dm::atan2(1.0, -2.0); }},
        {"exp(1)", 0x4005BF0A8B14576Aull, [] { return dm::exp(1.0); }},
        {"exp(-7.5)", 0x3F421F9BA40F31D5ull, [] { return dm::exp(-7.5); }},
        {"log(10)", 0x40026BB1BBB55516ull, [] { return dm::log(10.0); }},
        {"log2(3)", 0x3FF95C01A39FBD68ull, [] { return dm::log2(3.0); }},
        {"log10(7)", 0x3FEB0B0B0B78CC3Full, [] { return dm::log10(7.0); }},
        {"pow(1.5, 2.7)", 0x4007E859EFB1238Cull, [] { return dm::pow(1.5, 2.7); }},
        {"pow(2, 0.5)", 0x3FF6A09E667F3BCDull, [] { return dm::pow(2.0, 0.5); }},
        {"pow(0.9, 123.456)", 0x3EC2D24ABD025249ull, [] { return dm::pow(0.9, 123.456); }},
        {"sinh(0.3)", 0x3FD37D42AF54B926ull, [] { return dm::sinh(0.3); }},
        {"cosh(2)", 0x400E18FA0DF2D9BCull, [] { return dm::cosh(2.0); }},
        {"tanh(0.7)", 0x3FE356FB17AF2E91ull, [] { return dm::tanh(0.7); }},
    };
    for (const Golden& row : Table) {
        CAPTURE(row.what);
        CHECK(bitsOf(row.compute()) == row.bits);
    }
}

TEST_CASE("the functions are right, not only consistent")
{
    // Against this platform's own runtime, over a spread of arguments: within a
    // few units in the last place, which is what the runtimes promise of
    // themselves.
    std::mt19937_64 random(20260923);
    std::uniform_real_distribution<double> wide(-50.0, 50.0);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    std::uniform_real_distribution<double> positive(1e-6, 1e6);
    double worst = 0.0;
    for (int sample = 0; sample < 20000; ++sample) {
        const double x = wide(random);
        const double u = unit(random);
        const double p = positive(random);
        worst = std::max(worst, ulpsApart(dm::sin(x), std::sin(x)));
        worst = std::max(worst, ulpsApart(dm::cos(x), std::cos(x)));
        worst = std::max(worst, ulpsApart(dm::atan(x), std::atan(x)));
        worst = std::max(worst, ulpsApart(dm::atan2(x, u), std::atan2(x, u)));
        worst = std::max(worst, ulpsApart(dm::asin(u), std::asin(u)));
        worst = std::max(worst, ulpsApart(dm::acos(u), std::acos(u)));
        worst = std::max(worst, ulpsApart(dm::exp(x), std::exp(x)));
        worst = std::max(worst, ulpsApart(dm::log(p), std::log(p)));
        worst = std::max(worst, ulpsApart(dm::log10(p), std::log10(p)));
        worst = std::max(worst, ulpsApart(dm::pow(p, u * 3.0), std::pow(p, u * 3.0)));
        worst = std::max(worst, ulpsApart(dm::tanh(u), std::tanh(u)));
    }
    CHECK(worst <= 4.0);
}

TEST_CASE("what a script compares against a literal is exact")
{
    CHECK(dm::pow(2.0, 10.0) == 1024.0);
    CHECK(dm::pow(10.0, 2.0) == 100.0);
    CHECK(dm::pow(10.0, -2.0) == 0.01);
    CHECK(dm::pow(-2.0, 3.0) == -8.0);
    CHECK(dm::log10(1000.0) == 3.0);
    CHECK(dm::log2(8.0) == 3.0);
    CHECK(dm::sin(0.0) == 0.0);
    CHECK(dm::cos(0.0) == 1.0);
    CHECK(dm::exp(0.0) == 1.0);
    CHECK(dm::log(1.0) == 0.0);
    CHECK(dm::atan2(0.0, -1.0) == 3.1415926535897931);
}

TEST_CASE("the edges answer what C and IEEE 754 say they answer")
{
    constexpr double Inf = std::numeric_limits<double>::infinity();
    CHECK(std::isnan(dm::sin(Inf)));
    CHECK(std::isnan(dm::log(-1.0)));
    CHECK(dm::log(0.0) == -Inf);
    CHECK(dm::exp(1000.0) == Inf);
    CHECK(dm::exp(-1000.0) == 0.0);
    CHECK(std::isnan(dm::pow(-8.0, 1.0 / 3.0)));
    CHECK(dm::pow(0.0, -1.0) == Inf);
    CHECK(dm::pow(Inf, -1.0) == 0.0);
    CHECK(dm::pow(1.0, std::numeric_limits<double>::quiet_NaN()) == 1.0);
    CHECK(dm::pow(std::numeric_limits<double>::quiet_NaN(), 0.0) == 1.0);
    CHECK(std::isnan(dm::asin(1.5)));
    CHECK(dm::atan(Inf) == 1.5707963267948966);
}
