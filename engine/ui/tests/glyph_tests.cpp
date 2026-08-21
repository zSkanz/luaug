// The glyph store (M6, human decision 2026-08-21: "a cache, not a bake").
//
// These cases are about the SHAPE of the store rather than about how a letter
// looks, and the shape is the decision: keyed by face, size and codepoint,
// filled on demand, bounded, and with a chosen answer for a codepoint the face
// cannot draw. When M7 hands over a real face, these are the cases that say the
// key did not have to change.
#include "luaug/ui/ui.h"

#include <cmath>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using luaug::core::Color3;
using luaug::core::f32;
using luaug::core::Rect;
using luaug::core::Vec2;
using luaug::ui::buildTextGeometry;
using luaug::ui::DrawQuad;
using luaug::ui::glyphCacheStats;
using luaug::ui::measureText;
using luaug::ui::resetGlyphCache;

namespace {

[[nodiscard]] std::vector<DrawQuad> quadsOf(std::string_view text, std::string_view font = {}, f32 size = 12.0f)
{
    std::vector<DrawQuad> out;
    buildTextGeometry(text, font, size, 0.0f, Rect{Vec2{0.0f, 0.0f}, Vec2{200.0f, 40.0f}}, 0, 0,
                      Color3{1.0f, 1.0f, 1.0f}, 1.0f, 0, out);
    return out;
}

} // namespace

TEST_CASE("the store fills on demand and answers a repeat from what it has")
{
    resetGlyphCache();
    (void)measureText("abc", {}, 12.0f, 0.0f);
    const luaug::core::u64 afterFirst = glyphCacheStats().fills;
    CHECK(afterFirst == 3);

    // The property that makes this a cache rather than a bake, and the one a
    // second frame of a static label depends on: asking again costs nothing.
    (void)measureText("abc", {}, 12.0f, 0.0f);
    CHECK(glyphCacheStats().fills == afterFirst);
    CHECK(glyphCacheStats().hits >= 3);
}

TEST_CASE("size is part of the key, which is what stops M7 being a rewrite")
{
    // For THIS face the size half of the key is redundant, because a vector
    // glyph scales by multiplication. For a raster face it is not, and putting
    // it in the key now is the whole of the decision this file records.
    resetGlyphCache();
    (void)measureText("a", {}, 12.0f, 0.0f);
    (void)measureText("a", {}, 24.0f, 0.0f);
    CHECK(glyphCacheStats().fills == 2);
    CHECK(glyphCacheStats().entries == 2);
}

TEST_CASE("face is part of the key too, and a size below a quarter pixel is not")
{
    resetGlyphCache();
    (void)measureText("a", "asset://fonts/one.ttf", 12.0f, 0.0f);
    (void)measureText("a", "asset://fonts/two.ttf", 12.0f, 0.0f);
    CHECK(glyphCacheStats().fills == 2);

    // Quantized, because a `TextSize` a tween is animating would otherwise mint
    // an entry per frame -- and a quarter of a pixel is below what any face
    // resolves.
    const luaug::core::u64 before = glyphCacheStats().fills;
    (void)measureText("a", "asset://fonts/one.ttf", 12.05f, 0.0f);
    CHECK(glyphCacheStats().fills == before);
}

TEST_CASE("a codepoint the face cannot draw becomes a visible box")
{
    // The decision stated where it is read: a missing glyph that drew NOTHING
    // would make a Portuguese label silently lose its accents, and one that drew
    // `?` would be indistinguishable from a question mark somebody typed.
    resetGlyphCache();
    const std::vector<DrawQuad> boxes = quadsOf("ç");
    CHECK(glyphCacheStats().missingGlyphs == 1);
    // Four sides.
    CHECK(boxes.size() == 4);
    for (const DrawQuad& quad : boxes) {
        CHECK(quad.max.x > quad.min.x);
        CHECK(quad.max.y > quad.min.y);
    }
}

TEST_CASE("UTF-8 is decoded, so an accented word is one glyph per letter")
{
    // The failure this prevents is mojibake: reading the bytes one at a time
    // would make `ç` two glyphs, both of them boxes, and a label twice as wide
    // as it should be.
    resetGlyphCache();
    (void)measureText("ação", {}, 12.0f, 0.0f);
    // a, ç, ã, o -- four codepoints from six bytes, two of them missing, and
    // four entries because a missing glyph is cached under the character that
    // was ASKED for. Sharing one entry for every missing character would make
    // the counter say "1" for a label that is entirely boxes.
    CHECK(glyphCacheStats().fills == 4);
    CHECK(glyphCacheStats().missingGlyphs == 2);
}

TEST_CASE("an invalid byte advances by one and does not run off the end")
{
    // A truncated sequence is what a string cut mid-character looks like, and a
    // decoder that trusted the length byte would read past the view.
    resetGlyphCache();
    const std::string truncated = "a\xC3";
    const luaug::ui::TextRunMetrics metrics = measureText(truncated, {}, 12.0f, 0.0f);
    CHECK(metrics.size.x > 0.0f);
    CHECK(glyphCacheStats().missingGlyphs == 1);

    const std::string stray = "\x80\x80";
    (void)measureText(stray, {}, 12.0f, 0.0f);
    // Still one: every byte sequence that is not a character decodes to the same
    // "not text" codepoint and shares one entry, because that is one fact
    // however many times it happens. A missing LETTER is counted per letter.
    CHECK(glyphCacheStats().missingGlyphs == 1);
}

TEST_CASE("measurement and drawing agree about how wide a character is")
{
    // Both go through the same cached advance, which is what makes this true by
    // construction rather than by two implementations happening to match. A
    // centred label whose measure disagreed with its draw sits visibly off.
    resetGlyphCache();
    const luaug::ui::TextRunMetrics metrics = measureText("Hello", {}, 12.0f, 0.0f);
    const std::vector<DrawQuad> quads = quadsOf("Hello");
    REQUIRE_FALSE(quads.empty());

    f32 rightmost = 0.0f;
    for (const DrawQuad& quad : quads)
        rightmost = std::fmax(rightmost, quad.max.x);
    // The drawn ink ends inside the measured advance -- a glyph's cell is wider
    // than its ink, and it must never be narrower.
    CHECK(rightmost <= metrics.size.x + 0.001f);
    CHECK(rightmost > metrics.size.x * 0.5f);
}

TEST_CASE("a missing glyph still advances the pen")
{
    // Otherwise every letter after the accent piles up on the same column, which
    // is a worse failure than the box itself.
    resetGlyphCache();
    const luaug::ui::TextRunMetrics plain = measureText("aa", {}, 12.0f, 0.0f);
    const luaug::ui::TextRunMetrics accented = measureText("aça", {}, 12.0f, 0.0f);
    CHECK(accented.size.x > plain.size.x);
}

TEST_CASE("wrapping breaks on codepoints, never inside one")
{
    // A break landing inside a multi-byte character would produce two invalid
    // sequences out of one valid one -- two boxes where there was a letter.
    resetGlyphCache();
    const luaug::ui::TextRunMetrics wrapped = measureText("ção ção ção", {}, 12.0f, 30.0f);
    CHECK(wrapped.lineCount > 1);
    // Three distinct codepoints the face cannot draw, filled once each: if a
    // break had split one, there would be more.
    CHECK(glyphCacheStats().missingGlyphs == 2);
}
