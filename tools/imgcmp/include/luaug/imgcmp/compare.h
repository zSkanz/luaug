// Tolerance comparison of a rendered screenshot against a reference
// (roadmap M1 gate; architecture.md §9 "render tests").
//
// The verdict is deliberately two-dimensional -- a per-channel tolerance and a
// budget of differing pixels. One knob cannot express both: a caller who needs
// to allow three dithered pixels would otherwise have to accept an arbitrarily
// large error on every other pixel as well.
#pragma once

#include <cstddef>

#include "luaug/imgcmp/image.h"

namespace luaug::imgcmp
{

// Why a tolerance exists at all: the same frame rendered on WARP and on a real
// GPU legitimately lands a rounding step apart per channel. The defaults are
// deliberately tight -- they absorb rasterization rounding, not a changed image.
struct CompareOptions
{
    // Per-channel absolute delta that still counts as equal. A pixel differs
    // when ANY of its four channels exceeds this.
    int tolerance = 2;

    // Differing pixels still counted as a match. Zero means "no pixel may
    // differ", which is the right default for a golden screenshot.
    std::size_t maxDifferentPixels = 0;
};

enum class CompareStatus
{
    Match,
    PixelsDiffer,
    // The two images cannot be compared pixel-for-pixel: their dimensions
    // disagree, either is degenerate, or a buffer does not match its declared
    // size. An unverifiable image is never a match.
    SizeMismatch,
};

struct CompareResult
{
    CompareStatus status = CompareStatus::SizeMismatch;
    std::size_t differentPixels = 0;

    // Largest per-channel |actual - expected| anywhere in the image, including
    // pixels that stayed within tolerance. It answers "how much headroom is
    // left", which is what someone tuning --tolerance actually needs.
    int maxChannelDelta = 0;

    [[nodiscard]] bool passed() const noexcept { return status == CompareStatus::Match; }
};

// `options.tolerance` is expected to be in [0, 255]; the command line validates
// it, so nothing here silently rewrites a caller's input.
[[nodiscard]] CompareResult compare(const Image& actual, const Image& expected, const CompareOptions& options);

// Renders `actual` as a dimmed grayscale backdrop with every differing pixel
// painted opaque magenta, so a failure is readable at a glance instead of
// needing a pixel inspector.
//
// Contract: both images must satisfy the same preconditions compare() requires;
// returns an empty Image when they do not.
[[nodiscard]] Image renderDiff(const Image& actual, const Image& expected, const CompareOptions& options);

} // namespace luaug::imgcmp
