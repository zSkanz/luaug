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
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"
#include "luaug/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// The one translation unit that defines them. Both are
// implementation-in-header, so these includes ARE the definitions -- which is
// why nothing else in the module may include either.
//
// `stb_easy_font` is still here as the FALLBACK, and that is deliberate: a
// build whose content directory has no font file still draws text, and a test
// that runs without staged content still measures it. Text vanishing because an
// asset is missing is the failure mode a fallback exists to prevent.
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_easy_font.h>
#include <stb_truetype.h>

namespace luaug::ui {
namespace {

using core::Vec2;

// What `stb_easy_font` draws at scale 1: capitals are seven pixels tall and a
// line is twelve. Named because three places divide by them, and a magic 12 in
// three files is a magic 12 nobody can change.
// The default face's name, as `TextLabel.Font` spells it. An empty `Font` and
// this name are the same request.
constexpr std::string_view DefaultFaceName = "Inter";

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

    // Where this quad samples the atlas. Meaningless for the built-in vector
    // face, whose quads are solid rectangles -- the entry says which it is.
    f32 u0 = 0.0f;
    f32 v0 = 0.0f;
    f32 u1 = 0.0f;
    f32 v1 = 0.0f;
};

struct GlyphEntry
{
    u64 key = 0;
    f32 advance = 0.0f;
    u32 firstQuad = 0;
    u32 quadCount = 0;
    // Whether the quads sample the atlas or are solid rectangles. The two faces
    // differ here and nowhere else, which is what the M6 seam was built for.
    bool textured = false;
};

// A shelf packer: glyphs go left to right on a row whose height is the tallest
// glyph placed on it so far, and one that does not fit starts a new row. Not
// the tightest packing there is, and it does not need to be -- UI glyphs at one
// or two sizes are close to the same height, which is the case a shelf packer
// wastes nothing on.
struct AtlasPacker
{
    u32 cursorX = 0;
    u32 cursorY = 0;
    u32 rowHeight = 0;
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

    // Single-channel coverage. Empty until a raster face has drawn something,
    // and empty forever with the built-in one.
    std::vector<core::u8> atlas;
    u32 atlasWidth = 0;
    u32 atlasHeight = 0;
    u64 atlasVersion = 0;
    AtlasPacker packer;
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

// --- The faces ---------------------------------------------------------------

// A loaded face: the file's bytes, kept alive because `stbtt_fontinfo` points
// into them, and the metrics every size scales from.
struct Face
{
    u32 hash = 0;
    std::string name;
    std::vector<core::u8> data;
    stbtt_fontinfo info{};
    // Font units, from `hhea`. Scaled per size rather than stored per size,
    // because the scale is one multiplication and a per-size copy is a cache.
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    bool ready = false;
    // This name could not be loaded and resolves to the default face.
    //
    // Recorded rather than simply not cached, and the difference is a file read
    // per frame: a label drawn every frame with a typo in its font name would
    // otherwise re-attempt the load every frame. Recorded rather than left as a
    // not-ready face, because a not-ready face still has its OWN hash -- which
    // would key its glyphs separately from the default face it is drawing with,
    // and rasterise the same face twice.
    bool fallsBack = false;
};

struct FaceTable
{
    // Stable addresses: `faceFor` hands out references and a caller holds one
    // for the length of a `measureText`. A vector of values would move them all
    // the next time a name is looked up.
    std::vector<std::unique_ptr<Face>> faces;
    FaceProvider provider = nullptr;
    void* providerUser = nullptr;
    // Names that were asked for and could not be loaded. Kept so the warning is
    // once per name rather than once per frame -- a label drawn every frame with
    // a typo in its font name would otherwise fill the log.
    std::vector<u32> warned;
    bool defaultAttempted = false;
};

FaceTable& faceTable()
{
    static FaceTable instance;
    return instance;
}

// Where the default face lives beside the binary. Staged there by the build
// (engine/app/CMakeLists.txt) for the same reason the message catalog is: the
// engine resolves content relative to its own location, which is the shape a
// packaged build uses.
[[nodiscard]] std::filesystem::path defaultFacePath()
{
    return platform::paths().contentDir / "fonts" / "Inter.ttf";
}

// Loads a face's metrics. False leaves `ready` false, and the caller falls back.
[[nodiscard]] bool initFace(Face& face)
{
    if (face.data.empty())
        return false;
    // Offset 0: the file is a single-face TTF rather than a collection. A
    // collection would need `stbtt_GetFontOffsetForIndex`, and choosing WHICH
    // face out of one is a decision nobody has been asked to make.
    if (stbtt_InitFont(&face.info, face.data.data(), 0) == 0)
        return false;
    stbtt_GetFontVMetrics(&face.info, &face.ascent, &face.descent, &face.lineGap);
    face.ready = true;
    return true;
}

// The face for a name, loading it on first use. Never null: a name that cannot
// be loaded resolves to the default, and a default that cannot be loaded
// resolves to a face with `ready == false`, which is the built-in vector
// fallback.
[[nodiscard]] Face& faceFor(std::string_view name)
{
    FaceTable& table = faceTable();
    const u32 hash = faceHash(name);

    for (const std::unique_ptr<Face>& known : table.faces) {
        if (known->hash != hash || known->name != name)
            continue;
        // A name that failed to load resolves to the DEFAULT face, every time,
        // rather than to the empty entry that remembers it failed.
        return known->fallsBack ? faceFor({}) : *known;
    }

    Face face;
    face.hash = hash;
    face.name = std::string(name);

    // An empty name and the default's own name are the same request. Anything
    // else goes to the provider, which is the app's content mounts.
    const bool wantsDefault = name.empty() || name == DefaultFaceName;
    if (wantsDefault) {
        std::vector<std::byte> bytes;
        if (platform::readFile(defaultFacePath(), bytes)) {
            face.data.resize(bytes.size());
            std::memcpy(face.data.data(), bytes.data(), bytes.size());
        }
    }
    else if (table.provider != nullptr) {
        (void)table.provider(table.providerUser, name, face.data);
    }

    if (!initFace(face) && !wantsDefault) {
        // Named face, not loadable: fall back to the default rather than to
        // nothing. A label that vanished because its font name had a typo is a
        // bug report about the label.
        if (std::find(table.warned.begin(), table.warned.end(), hash) == table.warned.end()) {
            table.warned.push_back(hash);
            const core::I18nArg args[] = {{"font", std::string(name)}};
            core::log(core::LogLevel::Warn, LUAUG_TR("ui.warn.font_missing"), args);
        }
        face.fallsBack = true;
        table.faces.push_back(std::make_unique<Face>(std::move(face)));
        return faceFor({});
    }

    if (!face.ready && wantsDefault && !table.defaultAttempted) {
        // Once, and only for the default: a build whose content directory has no
        // font still draws text, with the built-in vector face, and should say
        // so rather than looking subtly wrong.
        table.defaultAttempted = true;
        const core::I18nArg args[] = {{"path", defaultFacePath().string()}};
        core::log(core::LogLevel::Warn, LUAUG_TR("ui.warn.default_font_missing"), args);
    }

    table.faces.push_back(std::make_unique<Face>(std::move(face)));
    return *table.faces.back();
}

// The multiplier between a cached glyph's units and pixels.
//
// ONE for a raster face and `pixelSize / 12` for the built-in vector one, and
// the difference is the whole reason the cache key has a size in it: a vector
// glyph is cached once and scaled, a raster glyph is rasterised at the size it
// will be drawn. Every caller multiplies by this and neither has to know which
// kind of face it is looking at.
[[nodiscard]] f32 scaleFor(const Face& face, f32 pixelSize) noexcept
{
    if (face.ready) {
        return 1.0f;
    }
    // A `TextSize` of 12 is the built-in face's own size. Below about 6 the
    // vector strokes collapse into each other, which is a property of the face
    // rather than something to clamp here -- a caller asking for 3-pixel text
    // gets 3-pixel text.
    return pixelSize / BuiltInLineHeight;
}

// The distance from one baseline to the next, in pixels.
[[nodiscard]] f32 lineHeightOf(const Face& face, f32 pixelSize) noexcept
{
    if (!face.ready) {
        return BuiltInLineHeight * scaleFor(face, pixelSize);
    }
    // `ascent - descent + lineGap`, which is the face's own opinion about line
    // spacing. `descent` is negative in font units, hence the subtraction.
    const f32 scale = stbtt_ScaleForPixelHeight(&face.info, pixelSize);
    return static_cast<f32>(face.ascent - face.descent + face.lineGap) * scale;
}

[[nodiscard]] f32 ascentOf(const Face& face, f32 pixelSize) noexcept
{
    if (!face.ready) {
        return BuiltInAscent * scaleFor(face, pixelSize);
    }
    return static_cast<f32>(face.ascent) * stbtt_ScaleForPixelHeight(&face.info, pixelSize);
}

// --- The atlas ---------------------------------------------------------------

// Square and fixed. 1024x1024 of single-channel coverage is one megabyte and
// holds several thousand glyphs at UI sizes, which is more distinct glyphs than
// a HUD has. Growing it would mean reuploading everything and re-deriving every
// cached UV, and the store already has a clear-and-refill path for the case
// where it genuinely fills up.
constexpr u32 AtlasSize = 1024;

// Rasterises one codepoint into the atlas at `pixelSize`, filling `entry`.
// False means it did not fit, which the caller answers by clearing the store.
[[nodiscard]] bool rasteriseGlyph(Face& face, f32 pixelSize, u32 codepoint, GlyphEntry& entry, GlyphStore& cache)
{
    const f32 scale = stbtt_ScaleForPixelHeight(&face.info, pixelSize);
    const int glyph = stbtt_FindGlyphIndex(&face.info, static_cast<int>(codepoint));
    if (glyph == 0) {
        return false;
    }

    int advanceUnits = 0;
    int bearingUnits = 0;
    stbtt_GetGlyphHMetrics(&face.info, glyph, &advanceUnits, &bearingUnits);
    entry.advance = static_cast<f32>(advanceUnits) * scale;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetGlyphBitmapBox(&face.info, glyph, scale, scale, &x0, &y0, &x1, &y1);
    const auto width = static_cast<u32>(x1 - x0);
    const auto height = static_cast<u32>(y1 - y0);

    entry.textured = true;
    if (width == 0 || height == 0) {
        // A space, and every other glyph with no ink. It has an advance and no
        // quad, which is exactly right -- and it must still be CACHED, or every
        // space in a paragraph is a rasterisation.
        entry.quadCount = 0;
        return true;
    }

    if (cache.atlas.empty()) {
        cache.atlas.assign(static_cast<usize>(AtlasSize) * AtlasSize, 0u);
        cache.atlasWidth = AtlasSize;
        cache.atlasHeight = AtlasSize;
    }

    // One texel of padding on each side, so bilinear sampling at the edge of a
    // glyph cannot reach into its neighbour. Without it, text at a fractional
    // scale grows faint marks nobody can account for.
    constexpr u32 Padding = 1;
    AtlasPacker& packer = cache.packer;
    if (packer.cursorX + width + Padding * 2 > cache.atlasWidth) {
        packer.cursorX = 0;
        packer.cursorY += packer.rowHeight;
        packer.rowHeight = 0;
    }
    if (packer.cursorY + height + Padding * 2 > cache.atlasHeight) {
        return false;
    }

    const u32 originX = packer.cursorX + Padding;
    const u32 originY = packer.cursorY + Padding;
    stbtt_MakeGlyphBitmap(&face.info, cache.atlas.data() + static_cast<usize>(originY) * cache.atlasWidth + originX,
                          static_cast<int>(width), static_cast<int>(height), static_cast<int>(cache.atlasWidth), scale,
                          scale, glyph);

    packer.cursorX += width + Padding * 2;
    packer.rowHeight = std::max(packer.rowHeight, height + Padding * 2);
    ++cache.atlasVersion;

    // The quad is in PIXELS at this size, measured from the top-left of the
    // line rather than from the baseline: everything downstream places text from
    // the top of its box, and converting once here beats converting at every
    // call site.
    const f32 ascentPixels = static_cast<f32>(face.ascent) * scale;
    entry.firstQuad = static_cast<u32>(cache.quads.size());
    entry.quadCount = 1;
    cache.quads.push_back(GlyphQuad{
        .minX = static_cast<f32>(x0),
        .minY = ascentPixels + static_cast<f32>(y0),
        .maxX = static_cast<f32>(x0) + static_cast<f32>(width),
        .maxY = ascentPixels + static_cast<f32>(y0) + static_cast<f32>(height),
        .u0 = static_cast<f32>(originX) / static_cast<f32>(cache.atlasWidth),
        .v0 = static_cast<f32>(originY) / static_cast<f32>(cache.atlasHeight),
        .u1 = static_cast<f32>(originX + width) / static_cast<f32>(cache.atlasWidth),
        .v1 = static_cast<f32>(originY + height) / static_cast<f32>(cache.atlasHeight),
    });
    return true;
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
[[nodiscard]] usize glyphIndex(Face& face, f32 pixelSize, u32 codepoint)
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
    const u64 key = glyphKey(face.hash, pixelSize, codepoint);

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
        // The atlas goes with them. Its packer hands out places by cursor, so
        // keeping the pixels while dropping the entries that name them would
        // leave a megabyte of coverage nothing can find and no room for more.
        cache.atlas.clear();
        cache.packer = AtlasPacker{};
        ++cache.atlasVersion;
        ++cache.stats.clears;
        cache.stats.entries = 0;
        return glyphIndex(face, pixelSize, codepoint);
    }

    GlyphEntry entry;
    entry.key = key;
    bool filled = false;
    if (face.ready) {
        filled = rasteriseGlyph(face, pixelSize, codepoint, entry, cache);
        if (!filled && !cache.atlas.empty() && cache.entries.size() > 0) {
            // The atlas is full rather than the codepoint being absent. Clearing
            // is the same answer the entry limit gets and for the same reason:
            // this is a signal that something is asking for a new size every
            // frame, not a routine eviction.
            const core::I18nArg args[] = {{"entries", static_cast<core::i64>(cache.entries.size())}};
            core::log(core::LogLevel::Warn, LUAUG_TR("ui.warn.glyph_cache_cleared"), args);
            cache.entries.clear();
            cache.quads.clear();
            cache.atlas.clear();
            cache.packer = AtlasPacker{};
            ++cache.atlasVersion;
            ++cache.stats.clears;
            cache.stats.entries = 0;
            return glyphIndex(face, pixelSize, codepoint);
        }
        if (!filled) {
            // The face has no glyph for this codepoint. The visible box, same
            // as the built-in face gives -- a player seeing boxes knows the
            // font is missing glyphs.
            fillReplacementGlyph(entry, cache.quads);
            entry.textured = false;
            ++cache.stats.missingGlyphs;
            filled = true;
        }
    }
    else if (drawable) {
        fillFaceGlyph(codepoint, entry, cache.quads);
        filled = true;
    }
    if (!filled) {
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
[[nodiscard]] f32 widthOf(std::string_view run, Face& face, f32 pixelSize)
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
void breakLines(std::string_view text, Face& face, f32 pixelSize, f32 scale, f32 maxWidth, std::vector<Line>& out)
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
    cache.atlas.clear();
    cache.packer = AtlasPacker{};
    ++cache.atlasVersion;
    cache.stats = GlyphCacheStats{};
}

TextRunMetrics measureText(std::string_view text, std::string_view font, f32 pixelSize, f32 maxWidth)
{
    // `font` SELECTS the face now rather than only keying the cache, which is
    // what took `TextLabel.Font` off the `Inert` list (roadmap M7). An empty
    // name is the default face; a name the provider cannot resolve falls back to
    // it and warns once.
    Face& face = faceFor(font);
    const f32 scale = scaleFor(face, pixelSize);
    std::vector<Line> lines;
    breakLines(text, face, pixelSize, scale, maxWidth, lines);

    TextRunMetrics metrics;
    metrics.lineCount = static_cast<u32>(lines.size());
    metrics.ascent = ascentOf(face, pixelSize);
    for (const Line& line : lines)
        metrics.size.x = std::fmax(metrics.size.x, line.width);
    metrics.size.y = static_cast<f32>(lines.size()) * lineHeightOf(face, pixelSize);
    return metrics;
}

void buildTextGeometry(std::string_view text, std::string_view font, f32 pixelSize, f32 maxWidth, core::Rect box,
                       i32 horizontalAlignment, i32 verticalAlignment, core::Color3 color, f32 alpha, u32 scissor,
                       std::vector<DrawQuad>& out)
{
    Face& face = faceFor(font);
    const f32 scale = scaleFor(face, pixelSize);
    std::vector<Line> lines;
    breakLines(text, face, pixelSize, scale, maxWidth, lines);

    const f32 lineHeight = lineHeightOf(face, pixelSize);
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
                // Texture 1 is the glyph atlas by convention (`ui.h`). A vector
                // glyph is a solid rectangle and samples nothing, which is
                // texture 0 -- the two faces differ here and in the metrics, and
                // nowhere else.
                glyph.texture = entry.textured ? 1u : 0u;
                glyph.uvMin = Vec2{shape.u0, shape.v0};
                glyph.uvMax = Vec2{shape.u1, shape.v1};
                glyph.scissor = scissor;
                out.push_back(glyph);
            }

            pen += entry.advance;
            index += decoded.length;
        }
        y += lineHeight;
    }
}

GlyphAtlas glyphAtlas() noexcept
{
    const GlyphStore& cache = store();
    return GlyphAtlas{cache.atlas, cache.atlasWidth, cache.atlasHeight, cache.atlasVersion};
}

void setFaceProvider(FaceProvider provider, void* user) noexcept
{
    FaceTable& table = faceTable();
    table.provider = provider;
    table.providerUser = user;
    // Loaded faces are dropped, not kept: a provider that has just been
    // installed may resolve a name the previous one could not, and a cached
    // fallback would be the wrong face for the rest of the process.
    table.faces.clear();
    table.warned.clear();
    resetGlyphCache();
}

} // namespace luaug::ui
