// The engine's own transcendentals (ADR 0083). See the header for why.
//
// **Contraction is off for this file**, on every compiler. A fused
// multiply-add rounds once where the written expression rounds twice, so a
// compiler that fuses -- Clang does by default on ARM, which is every Mac this
// engine runs on -- would give different bits from one that does not, which is
// the whole problem this file exists to remove. The build also passes
// `-ffp-contract=off`; the pragma is here so the file does not depend on it.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

#include "luaug/core/dmath.h"

#include <cmath>
#include <limits>

// The polynomial coefficients and the split constants below are the classic
// ones for double precision (the argument-reduction splits of pi/2 and ln 2,
// and minimax fits on the reduced ranges). They are numbers, not code: every
// line that uses them is this engine's own.
namespace luaug::core::dmath {
namespace {

constexpr double Infinity = std::numeric_limits<double>::infinity();
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// The only rounding to an integer these functions use: halves away from zero,
// by `floor`, which is exact on every platform and ignores the rounding mode.
[[nodiscard]] double roundHalfAway(double x) noexcept
{
    return x >= 0.0 ? std::floor(x + 0.5) : -std::floor(-x + 0.5);
}

// --- Double-double: a value held as an unevaluated sum of two doubles ---------
//
// For `pow`, whose answer is `exp(y * log(x))` and whose error is the error in
// `y * log(x)` magnified by `y`. Carried in two doubles, the product is good to
// about 2^-100, which leaves the final rounding as the only one that shows.

struct Dd
{
    double hi = 0.0;
    double lo = 0.0;
};

// a + b exactly, as a rounded sum and its error.
[[nodiscard]] Dd twoSum(double a, double b) noexcept
{
    const double s = a + b;
    const double bb = s - a;
    const double err = (a - (s - bb)) + (b - bb);
    return {s, err};
}

// Splits a double into two halves of 26 bits each, so their products are exact.
void split(double a, double& high, double& low) noexcept
{
    constexpr double Splitter = 134217729.0; // 2^27 + 1
    const double t = Splitter * a;
    high = t - (t - a);
    low = a - high;
}

// a * b exactly, as a rounded product and its error -- without a fused
// multiply-add, which is the one instruction this file may not use.
[[nodiscard]] Dd twoProduct(double a, double b) noexcept
{
    const double p = a * b;
    double ah = 0.0;
    double al = 0.0;
    double bh = 0.0;
    double bl = 0.0;
    split(a, ah, al);
    split(b, bh, bl);
    const double err = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
    return {p, err};
}

[[nodiscard]] Dd ddAdd(Dd a, Dd b) noexcept
{
    const Dd s = twoSum(a.hi, b.hi);
    const double lo = s.lo + a.lo + b.lo;
    return twoSum(s.hi, lo);
}

[[nodiscard]] Dd ddMul(Dd a, Dd b) noexcept
{
    const Dd p = twoProduct(a.hi, b.hi);
    const double lo = p.lo + (a.hi * b.lo + a.lo * b.hi);
    return twoSum(p.hi, lo);
}

[[nodiscard]] Dd ddMul(Dd a, double b) noexcept
{
    const Dd p = twoProduct(a.hi, b);
    const double lo = p.lo + a.lo * b;
    return twoSum(p.hi, lo);
}

// a / b, to double-double precision: one division and one correction.
[[nodiscard]] Dd ddDiv(Dd a, Dd b) noexcept
{
    const double q = a.hi / b.hi;
    const Dd qb = ddMul(b, q);
    const double r = ((a.hi - qb.hi) - qb.lo + a.lo) / b.hi;
    return twoSum(q, r);
}

// --- Reduction by pi/2 ---------------------------------------------------------

// pi/2 in three pieces of 33 bits each, so `n * piece` is exact for the
// quotients the reduction produces; the `T` constants are the tails.
constexpr double InvPio2 = 6.36619772367581382433e-01;
constexpr double Pio2_1 = 1.57079632673412561417e+00;
constexpr double Pio2_2 = 6.07710050630396597660e-11;
constexpr double Pio2_2t = 2.02226624879595063154e-21;
constexpr double Pio2_3 = 2.02226624871116645580e-21;
constexpr double Pio2_3t = 8.47842766036889956997e-32;

// Past this the three-piece reduction is no longer exact, and the argument is
// first brought into range by an exact `fmod` over 2 pi rounded to a double.
// That costs accuracy (a sine of 10^15 radians is noise on any machine) but
// not determinism: `fmod` is exact everywhere.
constexpr double ReductionLimit = 823549.6653; // 2^19 * pi/2

// x = n * pi/2 + (y0 + y1), with |y0 + y1| <= pi/4. Returns n mod 4.
int reducePio2(double x, double& y0, double& y1) noexcept
{
    if (std::fabs(x) >= ReductionLimit) {
        constexpr double TwoPi = 6.28318530717958647692;
        x = std::fmod(x, TwoPi);
    }
    const double n = roundHalfAway(x * InvPio2);
    // Three steps, each removing the next 33 bits of n * pi/2 and carrying the
    // rounding error of the last into the next.
    double r = x - n * Pio2_1;
    double t = r;
    double w = n * Pio2_2;
    r = t - w;
    w = n * Pio2_2t - ((t - r) - w);
    t = r;
    w = n * Pio2_3;
    r = t - w;
    w = n * Pio2_3t - ((t - r) - w);
    y0 = r - w;
    y1 = (r - y0) - w;
    const auto quadrant = static_cast<long long>(n);
    return static_cast<int>(quadrant & 3);
}

// sin(x + y) on [-pi/4, pi/4], `y` the tail of the reduced argument.
[[nodiscard]] double kernelSin(double x, double y) noexcept
{
    constexpr double S1 = -1.66666666666666324348e-01;
    constexpr double S2 = 8.33333333332248946124e-03;
    constexpr double S3 = -1.98412698298579493134e-04;
    constexpr double S4 = 2.75573137070700676789e-06;
    constexpr double S5 = -2.50507602534068634195e-08;
    constexpr double S6 = 1.58969099521155010221e-10;
    const double z = x * x;
    const double v = z * x;
    const double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

// cos(x + y) on [-pi/4, pi/4].
[[nodiscard]] double kernelCos(double x, double y) noexcept
{
    constexpr double C1 = 4.16666666666666019037e-02;
    constexpr double C2 = -1.38888888888741095749e-03;
    constexpr double C3 = 2.48015872894767294178e-05;
    constexpr double C4 = -2.75573143513906633035e-07;
    constexpr double C5 = 2.08757232129817482790e-09;
    constexpr double C6 = -1.13596475577881948265e-11;
    const double z = x * x;
    const double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    const double hz = 0.5 * z;
    const double w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + (z * r - x * y));
}

// --- exp and log kernels -------------------------------------------------------

constexpr double Ln2Hi = 6.93147180369123816490e-01;
constexpr double Ln2Lo = 1.90821492927058770002e-10;
constexpr double InvLn2 = 1.44269504088896338700e+00;

// ln 2 as a double-double, for `pow`.
constexpr Dd Ln2{6.93147180559945286227e-01, 2.31904681384629955842e-17};

// e^r * 2^k for a reduced r, |r| <= ln2/2, as the classic rational form.
[[nodiscard]] double expReduced(double hi, double lo, int k) noexcept
{
    constexpr double P1 = 1.66666666666666019037e-01;
    constexpr double P2 = -2.77777777770155933842e-03;
    constexpr double P3 = 6.61375632143793436117e-05;
    constexpr double P4 = -1.65339022054652515390e-06;
    constexpr double P5 = 4.13813679705723846039e-08;
    const double r = hi - lo;
    const double t = r * r;
    const double c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    const double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
    return std::ldexp(y, k);
}

// log(x) for a positive finite x, as a double-double, and the exponent it
// came from. The mantissa is brought into [sqrt(2)/2, sqrt(2)), f = m - 1 is
// exact, and log(1 + f) = 2 atanh(s) with s = f / (2 + f).
[[nodiscard]] Dd logDd(double x) noexcept
{
    int e = 0;
    double m = std::frexp(x, &e);
    if (m < 0.70710678118654752440) {
        m *= 2.0;
        e -= 1;
    }
    const double f = m - 1.0;
    // s = f / (2 + f) to double-double precision.
    const Dd two = twoSum(2.0, f);
    const Dd s = ddDiv(Dd{f, 0.0}, two);
    // 2 atanh(s) = 2s (1 + s^2/3 + s^4/5 + ...). |s| < 0.172, so the tail is
    // under 1% of the whole and a double carries it; fourteen terms put the
    // truncation below 2^-75 of the answer.
    const double z = s.hi * s.hi;
    double tail = 0.0;
    for (int k = 14; k >= 1; --k)
        tail = z * (1.0 / static_cast<double>(2 * k + 1) + tail);
    // 2s + 2s * tail, the first term carried in full.
    // (The loop above is Horner's rule over 1/3, 1/5, ... 1/29, evaluated in the
    // same order on every platform.)
    Dd result = ddAdd(Dd{2.0 * s.hi, 2.0 * s.lo}, twoProduct(2.0 * s.hi, tail));
    if (e != 0)
        result = ddAdd(ddMul(Ln2, static_cast<double>(e)), result);
    return result;
}

// y as an integer, when it is one that fits; `pow` treats those exactly.
[[nodiscard]] bool integral(double y) noexcept
{
    return std::floor(y) == y && std::fabs(y) < 9007199254740992.0;
}

[[nodiscard]] bool odd(double y) noexcept
{
    return integral(y) && std::fmod(std::fabs(y), 2.0) == 1.0;
}

} // namespace

// --- Trigonometry ------------------------------------------------------------------

double sin(double x) noexcept
{
    if (!std::isfinite(x))
        return NaN;
    if (std::fabs(x) <= 0.78539816339744830962)
        return x == 0.0 ? x : kernelSin(x, 0.0);
    double y0 = 0.0;
    double y1 = 0.0;
    switch (reducePio2(x, y0, y1)) {
    case 0:
        return kernelSin(y0, y1);
    case 1:
        return kernelCos(y0, y1);
    case 2:
        return -kernelSin(y0, y1);
    default:
        return -kernelCos(y0, y1);
    }
}

double cos(double x) noexcept
{
    if (!std::isfinite(x))
        return NaN;
    if (std::fabs(x) <= 0.78539816339744830962)
        return kernelCos(x, 0.0);
    double y0 = 0.0;
    double y1 = 0.0;
    switch (reducePio2(x, y0, y1)) {
    case 0:
        return kernelCos(y0, y1);
    case 1:
        return -kernelSin(y0, y1);
    case 2:
        return -kernelCos(y0, y1);
    default:
        return kernelSin(y0, y1);
    }
}

void sincos(double x, double& sine, double& cosine) noexcept
{
    if (!std::isfinite(x)) {
        sine = NaN;
        cosine = NaN;
        return;
    }
    if (std::fabs(x) <= 0.78539816339744830962) {
        sine = x == 0.0 ? x : kernelSin(x, 0.0);
        cosine = kernelCos(x, 0.0);
        return;
    }
    double y0 = 0.0;
    double y1 = 0.0;
    const int quadrant = reducePio2(x, y0, y1);
    const double ks = kernelSin(y0, y1);
    const double kc = kernelCos(y0, y1);
    switch (quadrant) {
    case 0:
        sine = ks;
        cosine = kc;
        break;
    case 1:
        sine = kc;
        cosine = -ks;
        break;
    case 2:
        sine = -ks;
        cosine = -kc;
        break;
    default:
        sine = -kc;
        cosine = ks;
        break;
    }
}

double tan(double x) noexcept
{
    double sine = 0.0;
    double cosine = 0.0;
    sincos(x, sine, cosine);
    return sine / cosine;
}

double atan(double x) noexcept
{
    constexpr double AtanHi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01,
                                 1.57079632679489655800e+00};
    constexpr double AtanLo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17,
                                 6.12323399573676603587e-17};
    constexpr double A[] = {3.33333333333329318027e-01,  -1.99999999998764832476e-01, 1.42857142725034663711e-01,
                            -1.11111104054623557880e-01, 9.09088713343650656196e-02,  -7.69187620504482999495e-02,
                            6.66107313738753120669e-02,  -5.83357013379057348645e-02, 4.97687799461593236017e-02,
                            -3.65315727442169155270e-02, 1.62858201153657823623e-02};
    if (std::isnan(x))
        return x;
    const bool negative = std::signbit(x);
    double a = std::fabs(x);
    if (a >= 7.3786976294838206464e19) // 2^66: atan is pi/2 to the last bit.
        return negative ? -(AtanHi[3] + AtanLo[3]) : AtanHi[3] + AtanLo[3];
    int id = -1;
    if (a < 0.4375) {
        if (a < 7.450580596923828125e-9) // 2^-27: atan(x) is x.
            return x;
    }
    else if (a < 1.1875) {
        if (a < 0.6875) {
            id = 0;
            a = (2.0 * a - 1.0) / (2.0 + a);
        }
        else {
            id = 1;
            a = (a - 1.0) / (a + 1.0);
        }
    }
    else if (a < 2.4375) {
        id = 2;
        a = (a - 1.5) / (1.0 + 1.5 * a);
    }
    else {
        id = 3;
        a = -1.0 / a;
    }
    const double z = a * a;
    const double w = z * z;
    const double s1 = z * (A[0] + w * (A[2] + w * (A[4] + w * (A[6] + w * (A[8] + w * A[10])))));
    const double s2 = w * (A[1] + w * (A[3] + w * (A[5] + w * (A[7] + w * A[9]))));
    if (id < 0)
        return negative ? -(a - a * (s1 + s2)) : a - a * (s1 + s2);
    const double result = AtanHi[id] - ((a * (s1 + s2) - AtanLo[id]) - a);
    return negative ? -result : result;
}

double atan2(double y, double x) noexcept
{
    constexpr double Pi = 3.1415926535897931160e+00;
    constexpr double PiLo = 1.2246467991473531772e-16;
    constexpr double PiO2 = 1.5707963267948965580e+00;
    constexpr double PiO4 = 7.8539816339744827900e-01;
    if (std::isnan(x) || std::isnan(y))
        return x + y;
    if (x == 1.0)
        return atan(y);
    const bool yNegative = std::signbit(y);
    const bool xNegative = std::signbit(x);
    if (y == 0.0) {
        if (!xNegative)
            return y; // +-0 by y's sign
        return yNegative ? -Pi : Pi;
    }
    if (x == 0.0)
        return yNegative ? -PiO2 : PiO2;
    if (std::isinf(x)) {
        if (std::isinf(y)) {
            const double angle = xNegative ? 3.0 * PiO4 : PiO4;
            return yNegative ? -angle : angle;
        }
        const double angle = xNegative ? Pi : 0.0;
        return yNegative ? -angle : angle;
    }
    if (std::isinf(y))
        return yNegative ? -PiO2 : PiO2;

    // The angle in the first quadrant, then placed.
    int ey = 0;
    int ex = 0;
    (void)std::frexp(y, &ey);
    (void)std::frexp(x, &ex);
    double z = 0.0;
    if (ey - ex > 60) {
        z = PiO2 + 0.5 * PiLo;
    }
    else if (xNegative && ey - ex < -60) {
        z = 0.0;
    }
    else {
        z = atan(std::fabs(y / x));
    }
    if (!xNegative)
        return yNegative ? -z : z;
    const double placed = Pi - (z - PiLo);
    return yNegative ? -placed : placed;
}

double asin(double x) noexcept
{
    if (std::isnan(x) || std::fabs(x) > 1.0)
        return NaN;
    // (1 - x)(1 + x) rather than 1 - x^2: near one, `1 - x` is exact.
    return atan2(x, std::sqrt((1.0 - x) * (1.0 + x)));
}

double acos(double x) noexcept
{
    if (std::isnan(x) || std::fabs(x) > 1.0)
        return NaN;
    return atan2(std::sqrt((1.0 - x) * (1.0 + x)), x);
}

// --- Exponentials and logarithms ----------------------------------------------

double exp(double x) noexcept
{
    constexpr double Overflow = 7.09782712893383973096e+02;
    constexpr double Underflow = -7.45133219101941108420e+02;
    if (std::isnan(x))
        return x;
    if (x > Overflow)
        return Infinity;
    if (x < Underflow)
        return 0.0;
    if (std::fabs(x) < 3.725290298461914e-09) // 2^-28: e^x is 1 + x.
        return 1.0 + x;
    const double k = roundHalfAway(x * InvLn2);
    const double hi = x - k * Ln2Hi;
    const double lo = k * Ln2Lo;
    return expReduced(hi, lo, static_cast<int>(k));
}

double log(double x) noexcept
{
    if (std::isnan(x) || x < 0.0)
        return NaN;
    if (x == 0.0)
        return -Infinity;
    if (std::isinf(x))
        return x;
    const Dd value = logDd(x);
    return value.hi + value.lo;
}

double log2(double x) noexcept
{
    if (std::isnan(x) || x < 0.0)
        return NaN;
    if (x == 0.0)
        return -Infinity;
    if (std::isinf(x))
        return x;
    // An exact power of two is its exponent, exactly.
    int e = 0;
    if (std::frexp(x, &e) == 0.5)
        return static_cast<double>(e - 1);
    constexpr Dd InvLn2Dd{1.44269504088896338700e+00, 2.03552737409310851174e-17};
    const Dd value = ddMul(logDd(x), InvLn2Dd);
    return value.hi + value.lo;
}

double log10(double x) noexcept
{
    if (std::isnan(x) || x < 0.0)
        return NaN;
    if (x == 0.0)
        return -Infinity;
    if (std::isinf(x))
        return x;
    // An exact power of ten is its exponent, exactly: 10^0 to 10^22 are the
    // powers a double holds without rounding.
    double power = 1.0;
    for (int k = 0; k <= 22; ++k) {
        if (x == power)
            return static_cast<double>(k);
        power *= 10.0;
    }
    constexpr Dd InvLn10{4.34294481903251816668e-01, 1.09831965021676510632e-17};
    const Dd value = ddMul(logDd(x), InvLn10);
    return value.hi + value.lo;
}

double pow(double x, double y) noexcept
{
    // The special cases, as C and IEEE 754 define them.
    if (y == 0.0 || x == 1.0)
        return 1.0;
    if (std::isnan(x) || std::isnan(y))
        return x + y;
    if (std::isinf(y)) {
        const double a = std::fabs(x);
        if (a == 1.0)
            return 1.0;
        return (a > 1.0) == (y > 0.0) ? Infinity : 0.0;
    }
    if (x == 0.0) {
        const bool oddInteger = odd(y);
        if (y < 0.0)
            return oddInteger ? (std::signbit(x) ? -Infinity : Infinity) : Infinity;
        return oddInteger ? x : 0.0;
    }
    if (std::isinf(x)) {
        const bool oddInteger = odd(y);
        if (x > 0.0)
            return y > 0.0 ? Infinity : 0.0;
        if (y > 0.0)
            return oddInteger ? -Infinity : Infinity;
        return oddInteger ? -0.0 : 0.0;
    }
    if (x < 0.0 && !integral(y))
        return NaN;

    const bool negative = x < 0.0 && odd(y);
    const double a = std::fabs(x);

    // A square root is a square root: correctly rounded by the standard, and
    // what Luau's own `^ 0.5` fast path computes, so the two cannot disagree.
    if (y == 0.5)
        return std::sqrt(a);

    // **A small integer power by squaring**, so `2^10` is 1024 and `10^2` is
    // 100 exactly -- which a script comparing against a literal expects, and
    // which an `exp(y log x)` answer a unit in the last place away would break.
    if (integral(y) && std::fabs(y) <= 64.0) {
        auto n = static_cast<long long>(std::fabs(y));
        double result = 1.0;
        double base = a;
        while (n > 0) {
            if ((n & 1) != 0)
                result *= base;
            base *= base;
            n >>= 1;
        }
        if (y < 0.0)
            result = 1.0 / result;
        return negative ? -result : result;
    }

    // e^(y log a), with y log a in double-double so the error is the final
    // rounding's and not y times the logarithm's.
    const Dd product = ddMul(logDd(a), y);
    if (product.hi > 709.8)
        return negative ? -Infinity : Infinity;
    if (product.hi < -745.2)
        return negative ? -0.0 : 0.0;
    const double k = roundHalfAway(product.hi * InvLn2);
    // hi - k ln2 in double-double, then the reduced exponential.
    const Dd reduced = ddAdd(product, ddMul(Ln2, -k));
    const double hi = reduced.hi;
    const double lo = -reduced.lo;
    const double result = expReduced(hi, lo, static_cast<int>(k));
    return negative ? -result : result;
}

// --- Hyperbolics ---------------------------------------------------------------

double sinh(double x) noexcept
{
    if (!std::isfinite(x))
        return x;
    const double a = std::fabs(x);
    double result = 0.0;
    if (a < 0.5) {
        // The series: e^a - e^-a cancels here, and the series does not.
        const double z = a * a;
        result =
            a + a * z *
                    (1.0 / 6.0 +
                     z * (1.0 / 120.0 + z * (1.0 / 5040.0 + z * (1.0 / 362880.0 +
                                                                 z * (1.0 / 39916800.0 + z * (1.0 / 6227020800.0))))));
    }
    else if (a < 710.0) {
        const double e = exp(a);
        result = 0.5 * (e - 1.0 / e);
    }
    else {
        // e^a overflows before sinh a does; half of e^(a/2) squared does not.
        const double e = exp(0.5 * a);
        result = (0.5 * e) * e;
    }
    return std::signbit(x) ? -result : result;
}

double cosh(double x) noexcept
{
    if (std::isnan(x))
        return x;
    const double a = std::fabs(x);
    if (a < 710.0) {
        const double e = exp(a);
        return 0.5 * (e + 1.0 / e);
    }
    const double e = exp(0.5 * a);
    return (0.5 * e) * e;
}

double tanh(double x) noexcept
{
    if (std::isnan(x))
        return x;
    const double a = std::fabs(x);
    double result = 0.0;
    if (a > 22.0) {
        result = 1.0;
    }
    else if (a < 0.5) {
        const double s = sinh(a);
        result = s / std::sqrt(1.0 + s * s);
    }
    else {
        const double t = exp(-2.0 * a);
        result = (1.0 - t) / (1.0 + t);
    }
    return std::signbit(x) ? -result : result;
}

} // namespace luaug::core::dmath

// **The one symbol Luau's VM and compiler call** (the patch in
// `third_party/patches/luau`): their `^` operator and its constant folding go
// through the same `pow` as everything else, so `2^0.5` in a script is the same
// bits on every platform. Global and not namespaced, because the patched Luau
// declares it that way and knows nothing of this engine.
double luaug_dpow(double a, double b)
{
    return luaug::core::dmath::pow(a, b);
}
