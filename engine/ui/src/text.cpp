// Text measurement and glyph geometry for the UI (ADR 0011, api-design.md
// §2.2's `TextLabel`).
//
// **v1's face is `stb_easy_font`, and that is a scope decision rather than a
// preference.** ADR 0011 names stb_truetype, and stb_truetype needs a TrueType
// FILE -- which is an asset with a licence, and adding one is a human decision
// under R6. So this milestone ships the built-in vector face that is already
// vendored inside `third_party/stb`, `TextLabel.Font` is marked `Inert` in the
// IDL with M7 named in its own doc, and the question "which font does the engine
// ship" is in `PROGRESS.md` under `Blocked — needs human`.
//
// What that costs, stated so nobody has to discover it: ASCII only, one weight,
// no kerning, and a fixed glyph shape that scales by multiplication. What it
// buys is that a label written in one line says something, that layout is
// deterministic with no file to load, and that the milestone's goldens exercise
// the whole 2D path rather than waiting on an asset decision.
//
// The seam is `measureText` and `buildTextGeometry`. When a real face arrives,
// they are the two functions that change and nothing above them does.
#include "luaug/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

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

[[nodiscard]] f32 scaleFor(f32 pixelSize) noexcept
{
    // A `TextSize` of 12 is the face's own size. Below about 6 the vector
    // strokes collapse into each other, which is a property of the face rather
    // than something to clamp here -- a caller asking for 3-pixel text gets
    // 3-pixel text.
    return pixelSize / BuiltInLineHeight;
}

// One measured line: how wide it is and which bytes it covers.
struct Line
{
    usize begin = 0;
    usize end = 0;
    f32 width = 0.0f;
};

[[nodiscard]] f32 widthOf(std::string_view run)
{
    if (run.empty())
        return 0.0f;
    // `stb_easy_font_width` takes a NUL-terminated `char*`, so a view has to be
    // copied. Bounded by the run, and a UI string is short: the alternative is
    // reimplementing the face's advance table to avoid one small copy.
    std::string copy(run);
    return static_cast<f32>(stb_easy_font_width(copy.data()));
}

// Breaks `text` into lines at `maxWidth`, at spaces, and mid-word only for a
// word wider than the box. A word cut at a random letter is worse than one that
// overhangs, which is what `TextWrapped`'s doc promises.
void breakLines(std::string_view text, f32 scale, f32 maxWidth, std::vector<Line>& out)
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
        line.width = widthOf(text.substr(lineBegin, end - lineBegin)) * scale;
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

        if (maxWidth > 0.0f) {
            const f32 width = widthOf(text.substr(lineBegin, index + 1 - lineBegin)) * scale;
            if (width > maxWidth && index > lineBegin) {
                if (lastSpace != std::string_view::npos && lastSpace > lineBegin) {
                    // Break at the space and drop it: a trailing space would
                    // make a centred line sit visibly left of centre.
                    const usize breakAt = lastSpace;
                    ++index;
                    flush(breakAt, breakAt + 1);
                    index = lineBegin;
                    continue;
                }
                flush(index, index);
                continue;
            }
        }
        ++index;
    }
    flush(text.size(), text.size());
}

} // namespace

TextRunMetrics measureText(std::string_view text, std::string_view font, f32 pixelSize, f32 maxWidth)
{
    // `font` is accepted and ignored, which is exactly what `Inert` on the
    // property declares. It is a parameter rather than an omission so that the
    // signature does not change when a face arrives.
    (void)font;

    const f32 scale = scaleFor(pixelSize);
    std::vector<Line> lines;
    breakLines(text, scale, maxWidth, lines);

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
    (void)font;

    const f32 scale = scaleFor(pixelSize);
    std::vector<Line> lines;
    breakLines(text, scale, maxWidth, lines);

    const f32 lineHeight = BuiltInLineHeight * scale;
    const f32 totalHeight = static_cast<f32>(lines.size()) * lineHeight;
    const f32 boxWidth = box.max.x - box.min.x;
    const f32 boxHeight = box.max.y - box.min.y;

    f32 y = box.min.y;
    if (verticalAlignment == 1)
        y += (boxHeight - totalHeight) * 0.5f;
    else if (verticalAlignment == 2)
        y += boxHeight - totalHeight;

    // `stb_easy_font_print` writes 16-byte vertices, four per quad. The buffer
    // is sized for a generous line rather than grown: a UI line past a thousand
    // characters is a bug in the caller, and truncating one is better than an
    // allocation per label per frame.
    static constexpr int VertexBytes = 16;
    static constexpr int MaxQuadsPerLine = 4096;
    std::vector<char> vertices(static_cast<usize>(MaxQuadsPerLine) * 4u * VertexBytes);

    for (const Line& line : lines) {
        f32 x = box.min.x;
        if (horizontalAlignment == 1)
            x += (boxWidth - line.width) * 0.5f;
        else if (horizontalAlignment == 2)
            x += boxWidth - line.width;

        std::string run(text.substr(line.begin, line.end - line.begin));
        const int quads =
            stb_easy_font_print(0.0f, 0.0f, run.data(), nullptr, vertices.data(), static_cast<int>(vertices.size()));

        for (int quad = 0; quad < quads; ++quad) {
            // The face emits axis-aligned rectangles as four corners; only the
            // first and third are needed, and reading them as floats out of the
            // byte buffer is what upstream's own example does.
            f32 corners[4];
            std::memcpy(&corners[0], vertices.data() + static_cast<usize>(quad * 4 * VertexBytes), sizeof(f32) * 2);
            std::memcpy(&corners[2], vertices.data() + static_cast<usize>((quad * 4 + 2) * VertexBytes),
                        sizeof(f32) * 2);

            DrawQuad glyph;
            glyph.min = Vec2{x + corners[0] * scale, y + corners[1] * scale};
            glyph.max = Vec2{x + corners[2] * scale, y + corners[3] * scale};
            glyph.color = color;
            glyph.alpha = alpha;
            glyph.texture = 0;
            glyph.scissor = scissor;
            out.push_back(glyph);
        }
        y += lineHeight;
    }
}

} // namespace luaug::ui
