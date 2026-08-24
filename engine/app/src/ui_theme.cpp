#include "luaug/app/ui_theme.h"

#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/core/log.h"
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if LUAUG_DEBUG_UI
#include <imgui.h>
#endif

namespace luaug::app {
namespace {

using core::Color3;
using core::f32;

// `#rrggbb`. A palette written as hex is a palette anybody can paste into a
// colour picker and check; written as three floats it is a palette nobody
// reads. Parsed at startup rather than stored as floats for the same reason
// `editor.json` writes hex -- there is one spelling of a colour in this
// repository and it is the one a designer uses.
[[nodiscard]] constexpr f32 channel(core::u32 packed, unsigned shift) noexcept
{
    return static_cast<f32>((packed >> shift) & 0xFFu) / 255.0f;
}

[[nodiscard]] constexpr Color3 rgb(core::u32 packed) noexcept
{
    return Color3{channel(packed, 16), channel(packed, 8), channel(packed, 0)};
}

// --- The themes -------------------------------------------------------------
//
// **Every number below was measured before it was committed**, and the numbers
// it replaced are the reason: `StyleColorsDark`'s disabled text is 0.50 grey on
// a 0.06 ground, which clears the bar, and its `TextDisabled` over a HOVERED row
// does not -- ImGui's default palette was drawn for a debug window on top of a
// game, not for an application somebody reads for six hours.
//
// The ratios are in `ui_theme_tests.cpp` as assertions, not as comments here,
// because a comment claiming 5.2:1 is a comment that stops being true the first
// time somebody nudges a hex digit.

constexpr Theme kDark{
    .id = "dark",
    .name = "LuauG Dark",
    .dark = true,
    .palette =
        {
            // Near-neutral with a slight blue cast, so the brand cyan sits in
            // the same family as the ground instead of on top of it.
            .background = rgb(0x1B1D21),
            .surface = rgb(0x141619),
            .surfaceRaised = rgb(0x2A2E35),
            .border = rgb(0x34383F),
            .text = rgb(0xDDE1E6),
            .textMuted = rgb(0x9AA1A9),
            // The mark's own colour, unmodified: on this ground it already
            // clears the bar, so the brand is the accent rather than a
            // near-miss of it.
            .accent = rgb(0x12B0FF),
            .onAccent = rgb(0x06121A),
            .warning = rgb(0xE0A34A),
            .danger = rgb(0xF87F73),
            .success = rgb(0x46C46E),
        },
};

constexpr Theme kLight{
    .id = "light",
    .name = "LuauG Light",
    .dark = false,
    .palette =
        {
            .background = rgb(0xF4F5F7),
            .surface = rgb(0xFFFFFF),
            .surfaceRaised = rgb(0xE7E9EC),
            .border = rgb(0xD0D4DA),
            .text = rgb(0x1A1D21),
            .textMuted = rgb(0x5B6169),
            // The same hue, darkened until it clears 4.5:1 against the LIGHTEST
            // ground it is drawn on, which is the raised surface rather than
            // the window. `#12B0FF` on this background is 2.2:1 -- the right
            // colour and the wrong value, which is exactly the case
            // `icons/README.md` says a single colour cannot cover.
            .accent = rgb(0x09629C),
            .onAccent = rgb(0xFFFFFF),
            .warning = rgb(0x8A5A00),
            .danger = rgb(0xB3261E),
            .success = rgb(0x16733C),
        },
};

constexpr Theme kThemes[]{kDark, kLight};

// sRGB -> linear, the specification's own piecewise transfer function.
[[nodiscard]] f32 linearize(f32 channelValue) noexcept
{
    return channelValue <= 0.04045f ? channelValue / 12.92f : std::pow((channelValue + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] f32 relativeLuminance(Color3 color) noexcept
{
    return 0.2126f * linearize(color.r) + 0.7152f * linearize(color.g) + 0.0722f * linearize(color.b);
}

} // namespace

std::span<const Theme> themes() noexcept
{
    return std::span<const Theme>(kThemes, std::size(kThemes));
}

const Theme& themeById(std::string_view id) noexcept
{
    for (const Theme& theme : kThemes) {
        if (theme.id == id)
            return theme;
    }
    return kThemes[0];
}

ThemeMetrics themeMetrics() noexcept
{
    return ThemeMetrics{};
}

f32 contrastRatio(Color3 a, Color3 b) noexcept
{
    const f32 first = relativeLuminance(a);
    const f32 second = relativeLuminance(b);
    const f32 lighter = std::max(first, second);
    const f32 darker = std::min(first, second);
    return (lighter + 0.05f) / (darker + 0.05f);
}

std::filesystem::path appearanceFile()
{
    const std::filesystem::path& userDir = platform::paths().userDir;
    return userDir.empty() ? std::filesystem::path{} : userDir / "appearance.json";
}

Appearance loadAppearance(const std::filesystem::path& file)
{
    Appearance appearance{.themeId = std::string(kThemes[0].id), .scale = 0.0f};
    if (file.empty())
        return appearance;

    std::string text;
    if (!platform::readTextFile(file, text))
        return appearance;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(text); !parsed.ok)
        return appearance;

    const core::JsonValue root = document.root();
    // Read through `themeById` rather than stored raw, so a file naming a theme
    // this build does not carry opens on the default instead of on nothing.
    appearance.themeId = std::string(themeById(root["theme"].asString()).id);
    if (const core::JsonValue scale = root["scale"]; scale.type() == core::JsonType::Number)
        appearance.scale = static_cast<f32>(scale.asNumber());
    return appearance;
}

bool saveAppearance(const std::filesystem::path& file, const Appearance& appearance)
{
    if (file.empty())
        return false;

    core::JsonWriter writer;
    writer.beginObject();
    writer.field("theme", appearance.themeId);
    writer.field("scale", static_cast<core::f64>(appearance.scale));
    writer.endObject();

    // SDL creates the preference directory on the way out of `SDL_GetPrefPath`,
    // but a file written into a directory somebody deleted mid-session is worth
    // one syscall to avoid.
    (void)platform::createDirectories(file.parent_path());
    return platform::writeTextFile(file, writer.text());
}

f32 resolveUiScale(f32 requested, f32 displayScale) noexcept
{
    // A display that answers zero or a nonsense number is a display with nothing
    // to say, and multiplying every padding in the shell by it would be worse
    // than ignoring it (`platform::windowDisplayScale` makes the same call).
    const f32 fromDisplay = displayScale > 0.0f ? displayScale : 1.0f;
    const f32 chosen = requested > 0.0f ? requested : fromDisplay;
    return std::clamp(chosen, kMinimumUiScale, kMaximumUiScale);
}

std::filesystem::path uiFontFile()
{
    return platform::paths().contentDir / "fonts" / "Inter.ttf";
}

#if LUAUG_DEBUG_UI

namespace {

[[nodiscard]] ImVec4 opaque(Color3 color) noexcept
{
    return ImVec4(color.r, color.g, color.b, 1.0f);
}

[[nodiscard]] ImVec4 fade(Color3 color, float alpha) noexcept
{
    return ImVec4(color.r, color.g, color.b, alpha);
}

// Between two colours, in sRGB. Used for the handful of slots that want a step
// between two tokens rather than a token of their own -- a scrollbar grab is
// not a design decision, it is "the border, a bit closer to the text".
[[nodiscard]] Color3 mix(Color3 from, Color3 to, float amount) noexcept
{
    return Color3{from.r + (to.r - from.r) * amount, from.g + (to.g - from.g) * amount,
                  from.b + (to.b - from.b) * amount};
}

} // namespace

void applyTheme(const Theme& theme, f32 scale)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return;

    const ThemePalette& p = theme.palette;
    const ThemeMetrics metrics = themeMetrics();

    // **From a default-constructed style, never from the live one.**
    // `ScaleAllSizes` multiplies in place, so applying a theme over an already
    // scaled style scales it twice -- which is invisible the first time and
    // grotesque the third.
    ImGuiStyle style;

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.45f;

    style.WindowPadding = ImVec2(metrics.windowPaddingX, metrics.windowPaddingY);
    style.FramePadding = ImVec2(metrics.framePaddingX, metrics.framePaddingY);
    style.ItemSpacing = ImVec2(metrics.itemSpacingX, metrics.itemSpacingY);
    style.ItemInnerSpacing = ImVec2(metrics.itemInnerSpacingX, metrics.itemInnerSpacingY);
    style.CellPadding = ImVec2(metrics.cellPaddingX, metrics.cellPaddingY);
    style.IndentSpacing = metrics.indentSpacing;
    style.ScrollbarSize = metrics.scrollbarSize;
    style.GrabMinSize = metrics.grabMinSize;

    // Square, everywhere there is a corner. Eleven members rather than one,
    // because ImGui has eleven -- and a shell that is square except for its
    // menu items is a shell somebody notices without being able to say why.
    style.WindowRounding = metrics.rounding;
    style.ChildRounding = metrics.rounding;
    style.PopupRounding = metrics.rounding;
    style.FrameRounding = metrics.rounding;
    style.ScrollbarRounding = metrics.rounding;
    style.GrabRounding = metrics.rounding;
    style.TabRounding = metrics.rounding;
    style.ImageRounding = metrics.rounding;
    style.MenuItemRounding = metrics.rounding;
    style.SelectableRounding = metrics.rounding;
    style.TreeLinesRounding = metrics.rounding;

    // With no radius, the line IS the edge. Every container gets one.
    style.WindowBorderSize = metrics.borderSize;
    style.ChildBorderSize = metrics.borderSize;
    style.PopupBorderSize = metrics.borderSize;
    style.FrameBorderSize = metrics.borderSize;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = metrics.tabBarBorderSize;
    style.TabBarOverlineSize = metrics.tabBarOverlineSize;
    style.DockingSeparatorSize = metrics.dockingSeparatorSize;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(16.0f, metrics.framePaddingY);

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    // Left, not centred. A column of buttons whose labels start at different x
    // positions is a column somebody has to read twice, and every button in
    // this shell that is wider than its text is wider because it was stretched.
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = opaque(p.text);
    colors[ImGuiCol_TextDisabled] = opaque(p.textMuted);
    colors[ImGuiCol_WindowBg] = opaque(p.background);
    colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    // Opaque rather than the default's 94%: a menu you can read the panel
    // through is a menu that is harder to read than the panel.
    colors[ImGuiCol_PopupBg] = opaque(p.background);
    colors[ImGuiCol_Border] = opaque(p.border);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = opaque(p.surface);
    colors[ImGuiCol_FrameBgHovered] = opaque(mix(p.surface, p.accent, 0.14f));
    colors[ImGuiCol_FrameBgActive] = opaque(mix(p.surface, p.accent, 0.24f));

    colors[ImGuiCol_TitleBg] = opaque(p.surface);
    colors[ImGuiCol_TitleBgActive] = opaque(p.surfaceRaised);
    colors[ImGuiCol_TitleBgCollapsed] = opaque(p.surface);
    colors[ImGuiCol_MenuBarBg] = opaque(p.surfaceRaised);

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = opaque(mix(p.border, p.textMuted, 0.25f));
    colors[ImGuiCol_ScrollbarGrabHovered] = opaque(p.textMuted);
    colors[ImGuiCol_ScrollbarGrabActive] = opaque(p.accent);

    colors[ImGuiCol_CheckMark] = opaque(p.accent);
    colors[ImGuiCol_CheckboxSelectedBg] = fade(p.accent, 0.20f);
    colors[ImGuiCol_SliderGrab] = opaque(p.accent);
    colors[ImGuiCol_SliderGrabActive] = opaque(mix(p.accent, p.text, 0.25f));

    // A button is the raised surface, not the accent. The accent is reserved for
    // what is selected and for the ONE primary action on a screen -- spend it on
    // every button and it stops meaning anything, which is the failure mode of
    // every palette that starts from a brand colour.
    colors[ImGuiCol_Button] = opaque(p.surfaceRaised);
    colors[ImGuiCol_ButtonHovered] = opaque(mix(p.surfaceRaised, p.accent, 0.22f));
    colors[ImGuiCol_ButtonActive] = opaque(mix(p.surfaceRaised, p.accent, 0.36f));

    colors[ImGuiCol_Header] = fade(p.accent, 0.24f);
    colors[ImGuiCol_HeaderHovered] = fade(p.accent, 0.16f);
    colors[ImGuiCol_HeaderActive] = fade(p.accent, 0.34f);

    colors[ImGuiCol_Separator] = opaque(p.border);
    colors[ImGuiCol_SeparatorHovered] = opaque(p.accent);
    colors[ImGuiCol_SeparatorActive] = opaque(p.accent);

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = fade(p.accent, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = opaque(p.accent);

    colors[ImGuiCol_InputTextCursor] = opaque(p.accent);

    colors[ImGuiCol_Tab] = opaque(p.surface);
    colors[ImGuiCol_TabHovered] = opaque(mix(p.surfaceRaised, p.accent, 0.20f));
    colors[ImGuiCol_TabSelected] = opaque(p.background);
    colors[ImGuiCol_TabSelectedOverline] = opaque(p.accent);
    colors[ImGuiCol_TabDimmed] = opaque(p.surface);
    colors[ImGuiCol_TabDimmedSelected] = opaque(p.background);
    // Muted rather than absent: an unfocused dock node still has a selected tab,
    // and hiding the mark makes the panel look like it has none.
    colors[ImGuiCol_TabDimmedSelectedOverline] = opaque(p.border);

    colors[ImGuiCol_DockingPreview] = fade(p.accent, 0.45f);
    colors[ImGuiCol_DockingEmptyBg] = opaque(p.surface);

    colors[ImGuiCol_PlotLines] = opaque(p.accent);
    colors[ImGuiCol_PlotLinesHovered] = opaque(mix(p.accent, p.text, 0.30f));
    colors[ImGuiCol_PlotHistogram] = opaque(p.accent);
    colors[ImGuiCol_PlotHistogramHovered] = opaque(mix(p.accent, p.text, 0.30f));

    colors[ImGuiCol_TableHeaderBg] = opaque(p.surfaceRaised);
    colors[ImGuiCol_TableBorderStrong] = opaque(p.border);
    colors[ImGuiCol_TableBorderLight] = opaque(mix(p.background, p.border, 0.60f));
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    // Barely there, and on purpose: banding a long property grid helps the eye
    // track a row across, and any more than this reads as two kinds of row.
    colors[ImGuiCol_TableRowBgAlt] = fade(theme.dark ? p.text : p.background, 0.03f);

    colors[ImGuiCol_TextLink] = opaque(p.accent);
    colors[ImGuiCol_TextSelectedBg] = fade(p.accent, 0.35f);
    colors[ImGuiCol_TreeLines] = opaque(p.border);
    colors[ImGuiCol_DragDropTarget] = opaque(p.accent);
    colors[ImGuiCol_DragDropTargetBg] = fade(p.accent, 0.15f);
    colors[ImGuiCol_UnsavedMarker] = opaque(p.warning);
    colors[ImGuiCol_NavCursor] = opaque(p.accent);
    colors[ImGuiCol_NavWindowingHighlight] = fade(p.accent, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

    // Sizes first, then the font: `ScaleAllSizes` deliberately does not touch
    // fonts, and the two multipliers have to agree or the padding around a
    // widget stops matching the text inside it.
    style.ScaleAllSizes(scale);
    style.FontSizeBase = metrics.fontSize;
    style.FontScaleMain = scale;

    ImGui::GetStyle() = style;
}

bool loadUiFont()
{
    if (ImGui::GetCurrentContext() == nullptr)
        return false;

    const std::filesystem::path file = uiFontFile();
    if (!platform::fileExists(file))
        return false;

    // Size zero would take ImGui's own default. The atlas is dynamic since 1.92,
    // so this is the size the face is DESIGNED against rather than the only one
    // it can be drawn at -- `PushFont(font, size)` still gets a real rasterisation
    // at any other size, which is what the launcher's heading uses.
    ImGuiIO& io = ImGui::GetIO();
    const ImFont* font = io.Fonts->AddFontFromFileTTF(file.string().c_str(), themeMetrics().fontSize);
    return font != nullptr;
}

#else

// ADR 0011 again: a shipping build has no ImGui to style. The data half above
// still compiles, and that is deliberate -- nothing about a palette needs a
// renderer, and a test that could only run in a dev build is a test that stops
// running the day somebody checks the shipping profile.

void applyTheme(const Theme&, f32)
{}

bool loadUiFont()
{
    return false;
}

#endif

} // namespace luaug::app
