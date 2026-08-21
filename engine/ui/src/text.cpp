// Text measurement and glyph geometry for the UI (ADR 0011, api-design.md
// §2.2's `TextLabel`).
//
// **v1's face is `stb_easy_font`, and that is a scope decision rather than a
// preference.** ADR 0011 names stb_truetype, and stb_truetype needs a TrueType
// FILE. The human chose Inter (OFL 1.1) as the default on 2026-08-21 and put its
// vendoring in M7 with the rest of the asset pipeline, so this milestone ships
// the built-in vector face already inside `third_party/stb` and `TextLabel.Font`
// stays `Inert` with M7 named in its own doc.
//
// What that costs, stated so nobody has to discover it: ASCII only, one weight,
// no kerning, and a fixed glyph shape that scales by multiplication.
//
// **The glyph store is a CACHE and not a bake, and that is the decision this
// file exists to have made** (human decision, 2026-08-21). An atlas baked once
// at boot works exactly as long as there is one face at one size, and that stops
// being true the moment M7 hands over a game's own font -- at which point a bake
// is a rewrite rather than a widening. So the store below is keyed by **face,
// size and codepoint** and filled on demand, while there is still exactly one
// face to fill it with. For a vector face the size half of that key is
// redundant, because the glyph scales by multiplication; for a raster one it is
// not, and putting it in now is the whole point.
//
// **Unicode is the same decision from the other side.** The face is ASCII and a
// game written in Portuguese already needs á ç ã õ, so the text is decoded as
// UTF-8 into codepoints and a codepoint the face cannot draw gets a **visible
// replacement box** rather than nothing, a question mark, or the mojibake that
// reading the bytes one at a time would produce. A player seeing boxes knows the
// font is missing glyphs; a player seeing `Ã¡` learns nothing.
//
// The seam is `measureText` and `buildTextGeometry`. When a real face arrives,
// the cache's key, its miss path and both signatures stay; what changes is what
// fills an entry.
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// The one translation unit that defines it. `stb_easy_font` is
// implementation-in-header with no separate guard, so this include IS the
// definition -- which is why nothing else in the module may include it.
#include <stb_easy_font.h>

namespace luaug::ui {
namespace {

using core::Vec2;

// What `stb_easy_font` draws at scale 1: capitals are seven pixels tall and a
// line is twelve. Named because three places divide by them, and a magic 12 in
// three files is a magic 12 nobody can change.
constexpr f32 BuiltInLineHeight = 12.0f;
constexpr f32 BuiltInAscent = 7.0f;

// The codepoints the built-in face covers: printable ASCII.
constexpr u32 FirstFaceCodepoint = 32;
constexpr u32 LastFaceCodepoint = 126;

// What a codepoint the face cannot draw becomes. Not a real character: it is the
// index the replacement box is cached under, above every Unicode scalar so it
// can never collide with one.
constexpr u32 ReplacementCodepoint = 0x11000000u;

// How many (face, size, codepoint) entries the store holds before it is emptied.
//
// **Clear-on-full rather than least-recently-used**, and the reason is honesty
// about what this milestone knows: an eviction policy is tuned against a real
// working set, and v1 has one face whose entire repertoire is ninety-five
// glyphs. Two thousand entries is twenty sizes of the whole face; reaching it
// means something is asking for a new size every frame, which is a bug worth a
// log line rather than a policy worth writing. M7's milestone -- the one with a
// face large enough to need one -- is where a policy is measured.
constexpr usize MaxGlyphEntries = 2048;

[[nodiscard]] f32 scaleFor(f32 pixelSize) noexcept
{
    // A `TextSize` of 12 is the face's own size. Below about 6 the vector
    // strokes collapse into each other, which is a property of the face rather
    // than something to clamp here -- a caller asking for 3-pixel text gets
    // 3-pixel text.
    return pixelSize / BuiltInLineHeight;
}

// --- UTF-8 -------------------------------------------------------------------

// One codepoint and how many bytes it took. An invalid sequence consumes exactly
// one byte and yields the replacement, so a malformed string advances rather
// than looping and never reads past its end.
struct Decoded
{
    u32 codepoint = ReplacementCodepoint;
    usize length = 1;
};

[[nodiscard]] Decoded decodeUtf8(std::string_view text, usize at) noexcept
{
    const auto byte = static_cast<unsigned char>(text[at]);
    if (byte < 0x80)
        return Decoded{byte, 1};

    usize length = 0;
    u32 codepoint = 0;
    if ((byte & 0xE0u) == 0xC0u) {
        length = 2;
        codepoint = byte & 0x1Fu;
    }
    else if ((byte & 0xF0u) == 0xE0u) {
        length = 3;
        codepoint = byte & 0x0Fu;
    }
    else if ((byte & 0xF8u) == 0xF0u) {
        length = 4;
        codepoint = byte & 0x07u;
    }
    else {
        return Decoded{};
    }

    if (at + length > text.size())
        return Decoded{};
    for (usize index = 1; index < length; ++index) {
        const auto continuation = static_cast<unsigned char>(text[at + index]);
        if ((continuation & 0xC0u) != 0x80u)
            return Decoded{};
        codepoint = (codepoint << 6) | (continuation & 0x3Fu);
    }
    return Decoded{codepoint, length};
}

// --- The glyph store ---------------------------------------------------------

// One glyph's geometry, in FACE units with the origin at the top left of its
// cell. The caller multiplies by the scale and adds the pen position, which is
// what makes one cached entry serve every size of a vector face -- and what a
// raster face will replace with an atlas rectangle without the key changing.
struct GlyphQuad
{
    f32 minX = 0.0f;
    f32 minY = 0.0f;
    f32 maxX = 0.0f;
    f32 maxY = 0.0f;
};

struct GlyphEntry
{
    u64 key = 0;
    f32 advance = 0.0f;
    u32 firstQuad = 0;
    u32 quadCount = 0;
};

struct GlyphStore
{
    // Sorted by key. A UI has a few hundred distinct glyphs at most, so a binary
    // search over a flat array beats a hash table on every axis that matters
    // here -- and a flat array has an order, which an unordered container does
    // not (R10).
    std::vector<GlyphEntry> entries;
    std::vector<GlyphQuad> quads;
    GlyphCacheStats stats;
};

GlyphStore& store()
{
    // Process-global like the layout stats beside it, and for the same reason:
    // the face is compiled in, so there is exactly one of it, and threading it
    // through `measureText` would put a cache in the signature of a pure
    // question.
    static GlyphStore instance;
    return instance;
}

// The key. Face, size and codepoint, packed: 16 bits of face hash, 16 of size in
// quarter-pixels, 32 of codepoint.
//
// The size is quantized rather than taken raw because a `TextSize` animated by a
// tween would otherwise mint an entry per frame, and a quarter of a pixel is
// below what any face resolves.
[[nodiscard]] u64 glyphKey(u32 face, f32 pixelSize, u32 codepoint) noexcept
{
    const f32 clamped = std::fmin(std::fmax(pixelSize, 0.0f), 16383.0f);
    const auto quarters = static_cast<u64>(clamped * 4.0f) & 0xFFFFu;
    return (static_cast<u64>(face & 0xFFFFu) << 48) | (quarters << 32) | static_cast<u64>(codepoint);
}

// FNV-1a over the font's content URN. A hash rather than an interned atom
// because `ui` is handed a `string_view` and interning would need the world.
[[nodiscard]] u32 faceHash(std::string_view font) noexcept
{
    u32 hash = 2166136261u;
    for (const char character : font) {
        hash ^= static_cast<u32>(static_cast<unsigned char>(character));
        hash *= 16777619u;
    }
    return hash;
}

// The face's own advance for a printable ASCII codepoint. Read from the table
// rather than through `stb_easy_font_width`, which rounds the whole string up:
// summing rounded per-glyph widths would drift from the line width the face
// actually draws.
[[nodiscard]] f32 faceAdvance(u32 codepoint) noexcept
{
    if (codepoint < FirstFaceCodepoint || codepoint > LastFaceCodepoint)
        return 0.0f;
    return static_cast<f32>(stb_easy_font_charinfo[codepoint - FirstFaceCodepoint].advance & 15);
}

// Appends the quads for one printable ASCII codepoint, in face units.
void fillFaceGlyph(u32 codepoint, GlyphEntry& entry, std::vector<GlyphQuad>& quads)
{
    entry.advance = faceAdvance(codepoint);

    // `stb_easy_font_print` writes 16-byte vertices, four per quad. One glyph is
    // a handful of segments; the buffer is sized for the worst of them.
    static constexpr int VertexBytes = 16;
    static constexpr int MaxQuadsPerGlyph = 64;
    char vertices[static_cast<usize>(MaxQuadsPerGlyph) * 4u * VertexBytes] = {};

    char text[2] = {static_cast<char>(codepoint), '\0'};
    const int count = stb_easy_font_print(0.0f, 0.0f, text, nullptr, vertices, static_cast<int>(sizeof(vertices)));

    entry.firstQuad = static_cast<u32>(quads.size());
    for (int quad = 0; quad < count; ++quad) {
        // The face emits axis-aligned rectangles as four corners; only the first
        // and third are needed, and reading them as floats out of the byte
        // buffer is what upstream's own example does.
        f32 corners[4];
        std::memcpy(&corners[0], vertices + static_cast<usize>(quad * 4 * VertexBytes), sizeof(f32) * 2);
        std::memcpy(&corners[2], vertices + static_cast<usize>((quad * 4 + 2) * VertexBytes), sizeof(f32) * 2);
        quads.push_back(GlyphQuad{corners[0], corners[1], corners[2], corners[3]});
    }
    entry.quadCount = static_cast<u32>(quads.size()) - entry.firstQuad;
}

// The replacement: a hollow box, one face unit thick, at the width of a question
// mark. **Visible on purpose.** A missing glyph that drew nothing would make a
// Portuguese label silently lose its accents, and one that drew `?` would be
// indistinguishable from a `?` somebody typed.
void fillReplacementGlyph(GlyphEntry& entry, std::vector<GlyphQuad>& quads)
{
    const f32 advance = faceAdvance('?');
    entry.advance = advance;
    entry.firstQuad = static_cast<u32>(quads.size());

    constexpr f32 top = 1.0f;
    constexpr f32 bottom = BuiltInAscent;
    const f32 left = 1.0f;
    const f32 right = std::fmax(advance - 1.0f, left + 2.0f);

    quads.push_back(GlyphQuad{left, top, right, top + 1.0f});
    quads.push_back(GlyphQuad{left, bottom - 1.0f, right, bottom});
    quads.push_back(GlyphQuad{left, top, left + 1.0f, bottom});
    quads.push_back(GlyphQuad{right - 1.0f, top, right, bottom});

    entry.quadCount = 4;
}

// The entry for one (face, size, codepoint), filling it if this is the first
// time anything asked. Returns an INDEX rather than a pointer, because filling
// may reallocate and a caller that held a pointer across the call would be
// holding a dangling one -- which is the classic way a cache becomes a crash.
[[nodiscard]] usize glyphIndex(u32 face, f32 pixelSize, u32 codepoint)
{
    GlyphStore& cache = store();
    const bool drawable = codepoint >= FirstFaceCodepoint && codepoint <= LastFaceCodepoint;
    // Cached under the codepoint that was ASKED for, even when the answer is the
    // replacement box, so `missingGlyphs` counts DISTINCT characters the face
    // cannot draw. Sharing one entry for all of them would make the counter say
    // "1" for a label that is entirely boxes, which is the number nobody needs.
    // The one exception is a byte sequence that is not a character at all: those
    // all decode to `ReplacementCodepoint` and share one entry, because "this is
    // not text" is one fact however many times it happens.
    const u64 key = glyphKey(face, pixelSize, codepoint);

    const auto position = std::lower_bound(cache.entries.begin(), cache.entries.end(), key,
                                           [](const GlyphEntry& entry, u64 value) { return entry.key < value; });
    if (position != cache.entries.end() && position->key == key) {
        ++cache.stats.hits;
        return static_cast<usize>(std::distance(cache.entries.begin(), position));
    }

    if (cache.entries.size() >= MaxGlyphEntries) {
        // See `MaxGlyphEntries`: reaching this is a signal rather than a routine
        // eviction, so it says so once per clear instead of silently churning.
        const core::I18nArg args[] = {{"entries", static_cast<core::i64>(cache.entries.size())}};
        core::log(core::LogLevel::Warn, LUAUG_TR("ui.warn.glyph_cache_cleared"), args);
        cache.entries.clear();
        cache.quads.clear();
        ++cache.stats.clears;
        cache.stats.entries = 0;
        return glyphIndex(face, pixelSize, codepoint);
    }

    GlyphEntry entry;
    entry.key = key;
    if (drawable) {
        fillFaceGlyph(codepoint, entry, cache.quads);
    }
    else {
        fillReplacementGlyph(entry, cache.quads);
        ++cache.stats.missingGlyphs;
    }

    const auto inserted = cache.entries.insert(position, entry);
    ++cache.stats.fills;
    cache.stats.entries = cache.entries.size();
    return static_cast<usize>(std::distance(cache.entries.begin(), inserted));
}

// The advance of one run, in face units. Every width in this file goes through
// the cache, so a measurement and a draw can never disagree about how wide a
// character is.
[[nodiscard]] f32 widthOf(std::string_view run, u32 face, f32 pixelSize)
{
    f32 total = 0.0f;
    usize index = 0;
    while (index < run.size()) {
        const Decoded decoded = decodeUtf8(run, index);
        total += store().entries[glyphIndex(face, pixelSize, decoded.codepoint)].advance;
        index += decoded.length;
    }
    return total;
}

// One measured line: how wide it is and which bytes it covers.
struct Line
{
    usize begin = 0;
    usize end = 0;
    f32 width = 0.0f;
};

// Breaks `text` into lines at `maxWidth`, at spaces, and mid-word only for a
// word wider than the box. A word cut at a random letter is worse than one that
// overhangs, which is what `TextWrapped`'s doc promises.
void breakLines(std::string_view text, u32 face, f32 pixelSize, f32 scale, f32 maxWidth, std::vector<Line>& out)
{
    out.clear();
    if (text.empty()) {
        out.push_back(Line{0, 0, 0.0f});
        return;
    }

    usize lineBegin = 0;
    usize lastSpace = std::string_view::npos;
    usize index = 0;

    const auto flush = [&](usize end, usize nextBegin) {
        Line line;
        line.begin = lineBegin;
        line.end = end;
        line.width = widthOf(text.substr(lineBegin, end - lineBegin), face, pixelSize) * scale;
        out.push_back(line);
        lineBegin = nextBegin;
        lastSpace = std::string_view::npos;
    };

    while (index < text.size()) {
        if (text[index] == '\n') {
            flush(index, index + 1);
            ++index;
            continue;
        }
        if (text[index] == ' ')
            lastSpace = index;

        // Advanced by CODEPOINT, so a multi-byte character is one unit of
        // wrapping rather than two or three -- and so a break can never land in
        // the middle of one and produce two invalid sequences.
        const Decoded decoded = decodeUtf8(text, index);

        if (maxWidth > 0.0f) {
            const f32 width =
                widthOf(text.substr(lineBegin, index + decoded.length - lineBegin), face, pixelSize) * scale;
            if (width > maxWidth && index > lineBegin) {
                if (lastSpace != std::string_view::npos && lastSpace > lineBegin) {
                    // Break at the space and drop it: a trailing space would
                    // make a centred line sit visibly left of centre.
                    const usize breakAt = lastSpace;
                    flush(breakAt, breakAt + 1);
                    index = lineBegin;
                    continue;
                }
                flush(index, index);
                continue;
            }
        }
        index += decoded.length;
    }
    flush(text.size(), text.size());
}

} // namespace

const GlyphCacheStats& glyphCacheStats() noexcept
{
    return store().stats;
}

void resetGlyphCache() noexcept
{
    GlyphStore& cache = store();
    cache.entries.clear();
    cache.quads.clear();
    cache.stats = GlyphCacheStats{};
}

TextRunMetrics measureText(std::string_view text, std::string_view font, f32 pixelSize, f32 maxWidth)
{
    // `font` reaches the cache's key and nothing else, which is exactly what
    // `Inert` on the property declares: there is one face, and naming another
    // gets you the same glyphs under a different key. When M7 hands over a real
    // face, this parameter is already where it has to be.
    const u32 face = faceHash(font);
    const f32 scale = scaleFor(pixelSize);
    std::vector<Line> lines;
    breakLines(text, face, pixelSize, scale, maxWidth, lines);

    TextRunMetrics metrics;
    metrics.lineCount = static_cast<u32>(lines.size());
    metrics.ascent = BuiltInAscent * scale;
    for (const Line& line : lines)
        metrics.size.x = std::fmax(metrics.size.x, line.width);
    metrics.size.y = static_cast<f32>(lines.size()) * BuiltInLineHeight * scale;
    return metrics;
}

void buildTextGeometry(std::string_view text, std::string_view font, f32 pixelSize, f32 maxWidth, core::Rect box,
                       i32 horizontalAlignment, i32 verticalAlignment, core::Color3 color, f32 alpha, u32 scissor,
                       std::vector<DrawQuad>& out)
{
    const u32 face = faceHash(font);
    const f32 scale = scaleFor(pixelSize);
    std::vector<Line> lines;
    breakLines(text, face, pixelSize, scale, maxWidth, lines);

    const f32 lineHeight = BuiltInLineHeight * scale;
    const f32 totalHeight = static_cast<f32>(lines.size()) * lineHeight;
    const f32 boxWidth = box.max.x - box.min.x;
    const f32 boxHeight = box.max.y - box.min.y;

    f32 y = box.min.y;
    if (verticalAlignment == 1)
        y += (boxHeight - totalHeight) * 0.5f;
    else if (verticalAlignment == 2)
        y += boxHeight - totalHeight;

    for (const Line& line : lines) {
        f32 x = box.min.x;
        if (horizontalAlignment == 1)
            x += (boxWidth - line.width) * 0.5f;
        else if (horizontalAlignment == 2)
            x += boxWidth - line.width;

        f32 pen = 0.0f;
        usize index = line.begin;
        while (index < line.end) {
            const Decoded decoded = decodeUtf8(text, index);
            const usize slot = glyphIndex(face, pixelSize, decoded.codepoint);
            // Copied out rather than referenced: the next `glyphIndex` may
            // reallocate the entry vector, and this loop calls it again.
            const GlyphEntry entry = store().entries[slot];

            for (u32 quad = 0; quad < entry.quadCount; ++quad) {
                const GlyphQuad& shape = store().quads[entry.firstQuad + quad];
                DrawQuad glyph;
                glyph.min = Vec2{x + (pen + shape.minX) * scale, y + shape.minY * scale};
                glyph.max = Vec2{x + (pen + shape.maxX) * scale, y + shape.maxY * scale};
                glyph.color = color;
                glyph.alpha = alpha;
                glyph.texture = 0;
                glyph.scissor = scissor;
                out.push_back(glyph);
            }

            pen += entry.advance;
            index += decoded.length;
        }
        y += lineHeight;
    }
}

} // namespace luaug::ui
