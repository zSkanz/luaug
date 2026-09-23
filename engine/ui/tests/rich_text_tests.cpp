// Rich text (F3): markup inside one `TextLabel`.
//
// Asserted as geometry rather than as pictures: a run's colour is the colour of
// its quads, bold is more quads, italic is a slant, a larger run is taller and
// shares the line's baseline -- and a tag this reader does not understand comes
// out as the characters somebody typed.
#include "luaug/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using luaug::core::Color3;
using luaug::core::f32;
using luaug::core::Rect;
using luaug::core::Vec2;
using luaug::ui::buildRichTextGeometry;
using luaug::ui::buildTextGeometry;
using luaug::ui::DrawQuad;
using luaug::ui::measureRichText;
using luaug::ui::measureText;
using luaug::ui::plainTextOf;

namespace {

const Rect Box{Vec2{0.0f, 0.0f}, Vec2{400.0f, 80.0f}};

[[nodiscard]] std::vector<DrawQuad> rich(std::string_view markup, f32 size = 12.0f)
{
    std::vector<DrawQuad> out;
    buildRichTextGeometry(markup, {}, size, 0.0f, Box, 0, 0, Color3{1.0f, 1.0f, 1.0f}, 1.0f, 0, out);
    return out;
}

[[nodiscard]] std::vector<DrawQuad> plain(std::string_view text, f32 size = 12.0f)
{
    std::vector<DrawQuad> out;
    buildTextGeometry(text, {}, size, 0.0f, Box, 0, 0, Color3{1.0f, 1.0f, 1.0f}, 1.0f, 0, out);
    return out;
}

[[nodiscard]] bool sameColour(const Color3& a, const Color3& b)
{
    return std::abs(a.r - b.r) < 1e-3f && std::abs(a.g - b.g) < 1e-3f && std::abs(a.b - b.b) < 1e-3f;
}

} // namespace

TEST_CASE("text with no tags draws exactly as plain text does")
{
    const std::vector<DrawQuad> marked = rich("Hello world");
    const std::vector<DrawQuad> unmarked = plain("Hello world");
    REQUIRE(marked.size() == unmarked.size());
    for (std::size_t index = 0; index < marked.size(); ++index) {
        CHECK(static_cast<double>(marked[index].min.x) == doctest::Approx(static_cast<double>(unmarked[index].min.x)));
        CHECK(static_cast<double>(marked[index].min.y) == doctest::Approx(static_cast<double>(unmarked[index].min.y)));
        CHECK(static_cast<double>(marked[index].max.x) == doctest::Approx(static_cast<double>(unmarked[index].max.x)));
    }
    CHECK(static_cast<double>(measureRichText("Hello world", {}, 12.0f, 0.0f).size.x) ==
          doctest::Approx(static_cast<double>(measureText("Hello world", {}, 12.0f, 0.0f).size.x)));
}

TEST_CASE("the tags come out and the entities go back in")
{
    CHECK(plainTextOf("<b>bold</b> and <font color=\"#ff0000\">red</font>") == "bold and red");
    CHECK(plainTextOf("1 &lt; 2 &amp;&amp; 3 &gt; 2") == "1 < 2 && 3 > 2");
    CHECK(plainTextOf("one<br/>two") == "one\ntwo");
}

TEST_CASE("a tag that is not understood is drawn as the text somebody typed")
{
    CHECK(plainTextOf("<blink>hi</blink>") == "<blink>hi</blink>");
    // A close with nothing of its kind open, and a font with nothing it can read.
    CHECK(plainTextOf("a</b>") == "a</b>");
    CHECK(plainTextOf("<font colour=\"red\">x</font>") == "<font colour=\"red\">x</font>");
    // An opening angle with no end is text too, and does not swallow the rest.
    CHECK(plainTextOf("a < b") == "a < b");
}

TEST_CASE("a font colour colours its run and only its run")
{
    const std::vector<DrawQuad> quads = rich("a<font color=\"#00ff00\">b</font>c");
    REQUIRE(quads.size() >= 3);
    bool green = false;
    bool white = false;
    for (const DrawQuad& quad : quads) {
        green = green || sameColour(quad.color, Color3{0.0f, 1.0f, 0.0f});
        white = white || sameColour(quad.color, Color3{1.0f, 1.0f, 1.0f});
    }
    CHECK(green);
    CHECK(white);

    // `rgb()` names the same colour.
    const std::vector<DrawQuad> same = rich("<font color=\"rgb(0, 255, 0)\">b</font>");
    REQUIRE_FALSE(same.empty());
    CHECK(sameColour(same.front().color, Color3{0.0f, 1.0f, 0.0f}));
}

TEST_CASE("bold is wider and drawn twice, italic leans, underline is a bar")
{
    const std::vector<DrawQuad> regular = rich("H");
    const std::vector<DrawQuad> bold = rich("<b>H</b>");
    CHECK(bold.size() == regular.size() * 2);
    CHECK(measureRichText("<b>HHHH</b>", {}, 12.0f, 0.0f).size.x > measureRichText("HHHH", {}, 12.0f, 0.0f).size.x);

    const std::vector<DrawQuad> italic = rich("<i>H</i>");
    REQUIRE_FALSE(italic.empty());
    CHECK(italic.front().slant > 0.0f);
    CHECK(static_cast<double>(regular.front().slant) == doctest::Approx(0.0));

    CHECK(rich("<u>H</u>").size() == regular.size() + 1);
    CHECK(rich("<s>H</s>").size() == regular.size() + 1);
}

TEST_CASE("a larger run makes its line taller and shares its baseline")
{
    const f32 single = measureRichText("ab", {}, 12.0f, 0.0f).size.y;
    const f32 mixed = measureRichText("a<font size=\"36\">b</font>", {}, 12.0f, 0.0f).size.y;
    CHECK(mixed > single * 2.0f);

    // Both letters stand on the same line: their lowest points agree to within
    // a pixel, which is what a baseline is.
    const std::vector<DrawQuad> quads = rich("H<font size=\"36\">H</font>");
    // The first letter's quads come first, as many as one `H` has.
    const std::size_t first = rich("H").size();
    REQUIRE(quads.size() == first * 2);
    f32 smallBottom = 0.0f;
    f32 largeBottom = 0.0f;
    for (std::size_t index = 0; index < quads.size(); ++index) {
        f32& bottom = index < first ? smallBottom : largeBottom;
        bottom = std::max(bottom, quads[index].max.y);
    }
    REQUIRE(smallBottom > 0.0f);
    CHECK(std::abs(largeBottom - smallBottom) <= 1.5f);
}

TEST_CASE("transparency fades a run, and nested tags close their own")
{
    const std::vector<DrawQuad> faded = rich("<font transparency=\"0.75\">x</font>");
    REQUIRE_FALSE(faded.empty());
    CHECK(static_cast<double>(faded.front().alpha) == doctest::Approx(0.25));

    // `<b>` inside a red font: the bold closes and the red carries on.
    const std::vector<DrawQuad> nested = rich("<font color=\"#ff0000\"><b>A</b>B</font>C");
    const std::vector<DrawQuad> each = rich("A");
    REQUIRE(nested.size() >= each.size() * 4);
    CHECK(sameColour(nested.front().color, Color3{1.0f, 0.0f, 0.0f}));
    CHECK(sameColour(nested.back().color, Color3{1.0f, 1.0f, 1.0f}));
}

TEST_CASE("markup wraps at spaces like plain text")
{
    std::vector<DrawQuad> out;
    buildRichTextGeometry("<b>one</b> two <i>three</i> four five six", {}, 12.0f, 60.0f, Box, 0, 0,
                          Color3{1.0f, 1.0f, 1.0f}, 1.0f, 0, out);
    const luaug::ui::TextRunMetrics metrics =
        measureRichText("<b>one</b> two <i>three</i> four five six", {}, 12.0f, 60.0f);
    CHECK(metrics.lineCount > 1);
    CHECK(metrics.size.x <= 60.0f + 1.0f);
}
