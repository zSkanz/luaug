// The glyph store (M6, human decision 2026-08-21: "a cache, not a bake").
//
// These cases are about the SHAPE of the store rather than about how a letter
// looks, and the shape is the decision: keyed by face, size and codepoint,
// filled on demand, bounded, and with a chosen answer for a codepoint the face
// cannot draw. When M7 hands over a real face, these are the cases that say the
// key did not have to change.
#include "luaug/ui/ui.h"

#include <cmath>
#include <cstring>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using luaug::core::Color3;
using luaug::core::f32;
using luaug::core::Rect;
using luaug::core::Vec2;
using luaug::ui::buildTextGeometry;
using luaug::ui::DrawQuad;
using luaug::ui::GlyphAtlas;
using luaug::ui::glyphAtlas;
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

TEST_CASE("two names that resolve to the same face share its glyphs")
{
    // Changed at M7, when `Font` stopped being `Inert` and started SELECTING a
    // face. Two names neither of which can be resolved both fall back to the
    // default, and two names for one face should share one set of glyphs --
    // caching them separately would rasterise the same face twice and spend an
    // atlas on it. Which face a name resolves to is what varies the key now.
    resetGlyphCache();
    (void)measureText("a", "asset://fonts/one.ttf", 12.0f, 0.0f);
    (void)measureText("a", "asset://fonts/two.ttf", 12.0f, 0.0f);
    CHECK(glyphCacheStats().fills == 1);

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

// ---------------------------------------------------------------------------
// The real face (roadmap M7, human decision 2026-08-20: Inter, OFL 1.1).

namespace {

// Reads the vendored font straight off disk. The face provider is how a project
// supplies a font at runtime; here it is how a test supplies one without a
// content system.
bool provideTestFace(void* user, std::string_view name, std::vector<luaug::core::u8>& out)
{
    (void)user;
    if (name != "asset://fonts/test.ttf") {
        return false;
    }
    std::ifstream file(LUAUG_TEST_FONT, std::ios::binary);
    if (!file) {
        return false;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out.resize(bytes.size());
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return !out.empty();
}

struct FaceGuard
{
    FaceGuard() { luaug::ui::setFaceProvider(&provideTestFace, nullptr); }
    ~FaceGuard() { luaug::ui::setFaceProvider(nullptr, nullptr); }
};

} // namespace

TEST_CASE("a real face rasterises into the atlas and measures by its own metrics")
{
    FaceGuard guard;

    // Nothing rasterised yet, so there is no atlas. A build with no font stays
    // in this state forever and still draws text with the built-in face, which
    // is the property the fallback exists for.
    resetGlyphCache();
    CHECK(glyphAtlas().pixels.empty());

    const luaug::ui::TextRunMetrics metrics = measureText("Hamburgefonstiv", "asset://fonts/test.ttf", 32.0f, 0.0f);
    CHECK(metrics.lineCount == 1);
    CHECK(metrics.size.x > 0.0f);
    // The line height is the FACE's own: `stbtt_ScaleForPixelHeight` makes
    // ascent-minus-descent exactly the requested size, and the line gap is added
    // on top -- so a face with no gap measures exactly 32 and one with a gap
    // measures more. Either is right; a constant twelve would not be.
    CHECK(metrics.size.y >= 32.0f);
    CHECK(metrics.size.y < 64.0f);
    CHECK(metrics.ascent > 0.0f);
    CHECK(metrics.ascent < metrics.size.y);

    const GlyphAtlas atlas = glyphAtlas();
    REQUIRE_FALSE(atlas.pixels.empty());
    CHECK(atlas.width == 1024);
    CHECK(atlas.height == 1024);
    CHECK(atlas.version > 0);
}

TEST_CASE("the rasterised face is Regular weight rather than the thinnest master")
{
    // **This is the case that justifies vendoring a VARIABLE font.** Upstream
    // ships no static TTF at the pinned tag, and stb_truetype knows nothing about
    // variation axes -- it draws the outlines in `glyf`, which for a variable
    // font is one particular master. If that master were Thin, every label in
    // the engine would be hairline and nobody would know why.
    //
    // Measured as INK COVERAGE of a capital H at 64 px: a Regular H covers
    // roughly a tenth of its own box, a Thin one a twentieth, a Black one nearly
    // a fifth. The band below is wide enough to survive a hinting change and
    // narrow enough to catch the wrong master.
    FaceGuard guard;
    resetGlyphCache();

    std::vector<luaug::ui::DrawQuad> quads;
    buildTextGeometry("H", "asset://fonts/test.ttf", 64.0f, 0.0f, luaug::core::Rect{{0.0f, 0.0f}, {200.0f, 200.0f}}, 0,
                      0, luaug::core::Color3{1.0f, 1.0f, 1.0f}, 1.0f, 0, quads);
    REQUIRE(quads.size() == 1);
    // Texture 1 is the glyph atlas: a rasterised glyph SAMPLES rather than being
    // a solid rectangle, which is the whole difference from the built-in face.
    CHECK(quads[0].texture == 1);
    CHECK(quads[0].uvMax.x > quads[0].uvMin.x);
    CHECK(quads[0].uvMax.y > quads[0].uvMin.y);

    const GlyphAtlas atlas = glyphAtlas();
    REQUIRE_FALSE(atlas.pixels.empty());

    const auto x0 = static_cast<luaug::core::usize>(quads[0].uvMin.x * static_cast<float>(atlas.width) + 0.5f);
    const auto y0 = static_cast<luaug::core::usize>(quads[0].uvMin.y * static_cast<float>(atlas.height) + 0.5f);
    const auto x1 = static_cast<luaug::core::usize>(quads[0].uvMax.x * static_cast<float>(atlas.width) + 0.5f);
    const auto y1 = static_cast<luaug::core::usize>(quads[0].uvMax.y * static_cast<float>(atlas.height) + 0.5f);
    REQUIRE(x1 > x0);
    REQUIRE(y1 > y0);

    double ink = 0.0;
    for (luaug::core::usize y = y0; y < y1; ++y) {
        for (luaug::core::usize x = x0; x < x1; ++x) {
            ink += static_cast<double>(atlas.pixels[y * atlas.width + x]) / 255.0;
        }
    }
    const double coverage = ink / static_cast<double>((x1 - x0) * (y1 - y0));

    // Measured against the glyph's own TIGHT box rather than the em box, which
    // is what the atlas rectangle is -- so the numbers are much higher than an
    // em-relative stem width would suggest. An H is two stems and a crossbar:
    // for Regular that is a bit under two fifths of its bounding box, for Thin
    // around an eighth, and for Black close to two thirds.
    CHECK(coverage > 0.28);
    CHECK(coverage < 0.50);
}
