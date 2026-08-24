#include "luaug/app/debug_overlay.h"

#include "luaug/app/streaming_host.h"

#if LUAUG_DEBUG_UI

#include "luaug/app/backends.h"
#include "luaug/app/icons.h"
#include "luaug/app/ui_theme.h"
#include "luaug/core/build_info.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/math.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/sdl_interop.h"
#include "luaug/platform/window.h"
#include "luaug/rhi/device.h"
#include "luaug/rhi/sdlgpu_interop.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/value.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

#include "icon_ids.gen.h"

#endif

namespace luaug::app {

#if LUAUG_DEBUG_UI

namespace {

// Bound at construction, read while drawing. These sit beside ImGui's own
// process-wide context rather than inside the class for two reasons: that
// context already makes a second live overlay meaningless, and keeping them out
// of the header is what lets the header stay free of SDL and of a layout that
// changes with the build profile.
//
// Main-thread only, like everything else that touches SDL's event queue.
platform::Window* g_window = nullptr;
const rhi::IDevice* g_device = nullptr;

// What this person chose to look at the engine through (ADR 0056), and what the
// display said when the window opened.
//
// Beside `g_window` and for the same reason: the Preferences dialog is a free
// function several call frames below anything holding an overlay, and the one
// live ImGui context already makes a second set of these meaningless.
Appearance g_appearance;
f32 g_displayScale = 1.0f;

// Re-styles ImGui and writes the choice back, which is one action rather than
// two: an appearance somebody changed and did not get back next launch is a
// setting they conclude does not work.
void applyAppearance()
{
    applyTheme(themeById(g_appearance.themeId), resolveUiScale(g_appearance.scale, g_displayScale));
    (void)saveAppearance(appearanceFile(), g_appearance);
}

// The theme currently drawing. Every call site that used to write a colour out
// by hand asks this instead.
[[nodiscard]] const ThemePalette& palette() noexcept
{
    return themeById(g_appearance.themeId).palette;
}

[[nodiscard]] ImVec4 themeColor(core::Color3 color) noexcept
{
    return ImVec4(color.r, color.g, color.b, 1.0f);
}

// A step between two tokens, for the handful of states that are "that colour,
// nearer this one" rather than a decision of their own -- a hovered primary
// button, a pressed one.
[[nodiscard]] ImVec4 themeBlend(core::Color3 from, core::Color3 to, f32 amount) noexcept
{
    return ImVec4(from.r + (to.r - from.r) * amount, from.g + (to.g - from.g) * amount,
                  from.b + (to.b - from.b) * amount, 1.0f);
}

// Frame time, sampled and held; everything else read directly.
//
// The rule this panel started with -- "nothing here is sampled or estimated" --
// is right for the three static facts below and was wrong for the one value
// that changes every frame. Printed raw at 60 Hz it cannot be read at all: the
// human reported it twice, and could only read the panel by pausing a frame.
//
// A held mean is also MORE honest about what the engine costs than a number
// that trembles, because windowed frames present through the swapchain and the
// last digits are VSync and the compositor rather than engine work. The worst
// frame in the window is printed beside it, since a hitch is what a developer
// is actually looking for and a mean is precisely the statistic that hides one.
//
// `frame.index` is gone. A bare counter at 60 Hz is unreadable by construction
// and answers nothing the frame time does not -- a stalled engine stops drawing
// this panel at all. The number still exists where it is used: the capture
// stream names its frames, and the baseline collector counts them.
struct FrameTimeMeter
{
    // Four hertz, the slow end of a readable range rather than the fast one: a
    // four-digit number that changes faster than this is legible only in
    // principle, which is the defect being fixed.
    static constexpr double kWindowSeconds = 0.25;

    double elapsed = 0.0;
    double sum = 0.0;
    double worst = 0.0;
    unsigned frames = 0;

    // What is displayed, replaced only when a window closes.
    double meanMs = 0.0;
    double worstMs = 0.0;
    double perSecond = 0.0;
    bool primed = false;

    void accumulate(double renderDt) noexcept
    {
        // The first frame has no previous one to measure against. Its zero is
        // kept out of the mean rather than divided by, which is the same guard
        // the raw print needed and for the same reason.
        if (renderDt > 0.0) {
            sum += renderDt;
            worst = renderDt > worst ? renderDt : worst;
            ++frames;
        }

        elapsed += renderDt;
        if (elapsed < kWindowSeconds || frames == 0)
            return;

        const double mean = sum / static_cast<double>(frames);
        meanMs = mean * 1000.0;
        worstMs = worst * 1000.0;
        perSecond = 1.0 / mean;
        primed = true;

        elapsed = 0.0;
        sum = 0.0;
        worst = 0.0;
        frames = 0;
    }
};

// Accumulates only while the panel is drawing, which is what makes the window
// it reports the window it displayed.
FrameTimeMeter g_frameTime;

// Three facts the host already knows, plus the sampled frame time above.
void drawStats(const Frame& frame)
{
    g_frameTime.accumulate(frame.renderDt);

    // Dashes rather than a made-up 0.00 before the first window closes: a
    // quarter second of "no measurement yet" is honest and 0.00 ms is not.
    if (g_frameTime.primed) {
        ImGui::Text("%.2f ms (%.0f fps)  worst %.2f ms", g_frameTime.meanMs, g_frameTime.perSecond,
                    g_frameTime.worstMs);
    }
    else {
        ImGui::TextUnformatted("-- ms (-- fps)");
    }

    const std::string_view backend = backendName(g_device->backend());
    ImGui::Text("backend %.*s", static_cast<int>(backend.size()), backend.data());

    const platform::WindowSize size = platform::windowPixelSize(*g_window);
    ImGui::Text("drawable %d x %d", size.width, size.height);
}

// Which panel the editor is drawing on, from the panel's own background.
//
// **Decided rather than configured**, which is what makes it follow an ImGui
// style change for free and leaves nothing to keep in sync. Relative luminance
// with the usual coefficients, and the threshold is the middle: a background
// under it is a dark panel and the palette's `dark` value is the one that clears
// 3:1 against it.
[[nodiscard]] IconAtlas::Panel currentPanel() noexcept
{
    const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const float luminance = 0.2126f * background.x + 0.7152f * background.y + 0.0722f * background.z;
    return luminance < 0.5f ? IconAtlas::Panel::Dark : IconAtlas::Panel::Light;
}

// The colour an icon is drawn in: its ROLE's, or the panel's own foreground.
//
// The fallback is not a degraded path -- it is what every icon did before there
// was a palette, and it is what all of them do when tinting is off. The set was
// drawn and collision-checked in a single ink, so an uncoloured editor is not a
// worse one.
[[nodiscard]] ImVec4 iconTint(const IconAtlas* icons, std::string_view id) noexcept
{
    if (icons != nullptr) {
        if (const std::optional<core::Color3> role = icons->tintFor(id, currentPanel()); role.has_value()) {
            // Alpha from the panel's own text colour, so a disabled row's icon
            // still dims with its label -- the role says WHICH colour and the
            // style says how present it is.
            const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            return ImVec4(role->r, role->g, role->b, text.w);
        }
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

// One icon, inline, at the current cursor. Returns false when there is no atlas
// or no cell, so a caller can fall back to text rather than leaving a hole.
//
// **Tinted with the current text colour**, which is the whole reason the source
// images are white masks: one drawing serves a light panel and a dark one, and
// a disabled row's icon dims with its label for free. `ImageWithBg` rather than
// `Image` because the tint parameter moved there in ImGui 1.91.9.
bool drawIcon(const IconAtlas* icons, std::string_view id, float size,
              std::optional<core::Color3> override = std::nullopt)
{
    if (icons == nullptr || !icons->ready())
        return false;

    const IconSprite sprite = icons->find(id, static_cast<core::u32>(size + 0.5f));
    if (!sprite.valid)
        return false;

    SDL_GPUTexture* native = g_device != nullptr ? rhi::nativeTexture(*g_device, icons->texture()) : nullptr;
    if (native == nullptr)
        return false;

    // **A colour a PERSON put on this folder beats the role's**, and it takes
    // the panel's own alpha the way a role does, so a disabled row's coloured
    // icon still dims with its label. The role says what KIND of thing an icon
    // is; this says which one, and only a person can say that.
    ImVec4 tint = iconTint(icons, id);
    if (override.has_value())
        tint = ImVec4(override->r, override->g, override->b, tint.w);

    ImGui::ImageWithBg(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(native)), ImVec2(size, size),
                       ImVec2(sprite.u0, sprite.v0), ImVec2(sprite.u1, sprite.v1), ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
                       tint);
    return true;
}

// The badge over an icon already drawn at `size`, with its top-left at `origin`
// in SCREEN space (ADR 0049's mark, `icons/README.md`'s `overlay.` namespace).
//
// **Two draws, in this order, every time.** First `overlay.StampBase` -- the
// solid silhouette -- in the panel's own BACKGROUND colour and slightly larger,
// which punches a clean hole in whatever is underneath. Then `overlay.Stamp` --
// the same silhouette with the mark cut out -- in the foreground, centred in
// that hole.
//
// **The knockout is what makes the badge exist.** Measured across the class set
// at 16 px, 37 of 42 icons already have ink where the badge goes -- 51% under
// `class.Workspace` and 49% under `class.Folder` -- so a bare badge lands on a
// folder's body and is not there.
//
// Drawn through the window's draw list rather than as an ImGui item, because it
// is not one: it sits ON a row that has already been laid out, it takes no
// space and no clicks, and an `Image` here would take both.
//
// **Its colour is its own.** A badge means the same thing on every icon, so it
// takes its own role rather than the subject's -- tinting it `spatial` on a
// `Part` and `ui` on a `Frame` would make one mark's colour mean two things.
void drawIconBadge(const IconAtlas* icons, ImVec2 origin, float size)
{
    if (icons == nullptr || !icons->ready() || size <= 0.0f)
        return;

    SDL_GPUTexture* native = g_device != nullptr ? rhi::nativeTexture(*g_device, icons->texture()) : nullptr;
    if (native == nullptr)
        return;

    const IconAtlas::Overlay& overlay = icons->overlay();
    const float mark = size * overlay.scale;
    const float halo = mark * overlay.haloScale;
    if (!(mark > 0.0f))
        return;

    const IconSprite base = icons->find(icons::OverlayStampBase, static_cast<core::u32>(halo + 0.5f));
    const IconSprite face = icons->find(icons::OverlayStamp, static_cast<core::u32>(mark + 0.5f));
    if (!base.valid || !face.valid)
        return;

    // The corner the theme asked for, measured from the icon's own box.
    const bool right = overlay.corner == IconAtlas::Overlay::Corner::BottomRight ||
                       overlay.corner == IconAtlas::Overlay::Corner::TopRight;
    const bool bottom = overlay.corner == IconAtlas::Overlay::Corner::BottomRight ||
                        overlay.corner == IconAtlas::Overlay::Corner::BottomLeft;

    // **Both are centred on one point**, which is what keeps the rim even. The
    // two files share an outer silhouette exactly, so a badge positioned by its
    // corner instead would put the whole difference on one side.
    const ImVec2 centre(origin.x + (right ? size - halo * 0.5f : halo * 0.5f),
                        origin.y + (bottom ? size - halo * 0.5f : halo * 0.5f));

    const auto quad = [&centre](float edge) {
        const float half = edge * 0.5f;
        return std::pair<ImVec2, ImVec2>{ImVec2(centre.x - half, centre.y - half),
                                         ImVec2(centre.x + half, centre.y + half)};
    };

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto [haloMin, haloMax] = quad(halo);
    const auto [markMin, markMax] = quad(mark);

    // The panel's own background, so the hole matches whatever the row is
    // painted on -- including a selected row, which is a different colour from
    // the window behind it.
    const ImVec4 behind = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImVec4 front = iconTint(icons, icons::OverlayStamp);

    const auto texture = static_cast<ImTextureID>(reinterpret_cast<intptr_t>(native));
    draw->AddImage(texture, haloMin, haloMax, ImVec2(base.u0, base.v0), ImVec2(base.u1, base.v1),
                   ImGui::ColorConvertFloat4ToU32(ImVec4(behind.x, behind.y, behind.z, 1.0f)));
    draw->AddImage(texture, markMin, markMax, ImVec2(face.u0, face.v0), ImVec2(face.u1, face.v1),
                   ImGui::ColorConvertFloat4ToU32(front));
}

// A button whose face is an icon, with the word as its fallback and its tooltip.
//
// Six drawn words in a row is a sentence somebody reads; six pictures is a
// control panel they aim at. Falls back to the word when there is no atlas or no
// cell for the id, because a button with nothing on it is not a smaller button
// -- and six `action.` ids are drawn tomorrow, so that path is live rather than
// theoretical.
//
// `strId` rather than the label, because the label changes with state (play
// becomes stop) and an ImGui id that changes with state is a button that loses
// its press half way through.
// `frameless` drops the button's background, its BORDER and its padding, leaving
// the picture and its hit box. All three, because a frame is not one thing: a
// transparent fill with the theme's outline still on it is a grey square around
// a sixteen-pixel glyph, which is what a chevron column looked like the day the
// shell learned to draw borders.
//
// It also removes a trap worth naming, because this file fell into it: a FRAMED
// button occupies its face plus the theme's padding, so a caller centring one by
// the size of the picture puts the picture in the middle of its row and the
// frame half a padding outside it, top and bottom. Frameless, the button IS the
// picture, and centring by the face is the same number as centring by the box. That is what a tree's chevron and its
// plus want: a filled rounded rectangle behind a sixteen-pixel glyph is a button drawn around a control that did not
// need one, and a column of them is a column of boxes. The affordance is the icon and the tooltip; the toolbar keeps
// its frames, because a toolbar button IS a button.
bool iconButton(const IconAtlas* icons, std::string_view id, float size, const char* strId, const char* word,
                const char* tip, bool frameless = false, bool fillRowHeight = false)
{
    if (frameless) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        // **`fillRowHeight` is what puts a toolbar's icons on the same line as
        // its text field.** Frameless normally means the button IS the picture,
        // which is what a tree row wants -- it places its chevron by hand at an
        // exact pitch, and a taller button would overflow the row. A TOOLBAR
        // wants the opposite: the tallest thing on the line is an input at frame
        // height, and an item shorter than it sits at the top of the line rather
        // than in the middle of it. Padding the button out to frame height
        // centres the picture inside it, which is one number rather than a
        // cursor nudge before every item -- and a nudge makes the line taller,
        // which is the gap that shows up under the tab bar.
        const float pad = fillRowHeight ? (ImGui::GetFrameHeight() - size) * 0.5f : 0.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, pad > 0.0f ? pad : 0.0f));
        // **A frame is a fill AND a border, and the border was surviving.**
        // `ImageButton` renders both, so a transparent `ImGuiCol_Button` leaves
        // the outline behind -- which is invisible while the theme draws no
        // borders and is a grey square around every chevron the moment it does.
        // Frameless has to mean both halves or it means neither.
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    }
    const auto unstyle = [frameless]() {
        if (frameless) {
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
        }
    };

    bool pressed = false;
    if (icons != nullptr && icons->ready() && icons->has(id)) {
        const IconSprite sprite = icons->find(id, static_cast<core::u32>(size + 0.5f));
        SDL_GPUTexture* native = g_device != nullptr ? rhi::nativeTexture(*g_device, icons->texture()) : nullptr;
        if (sprite.valid && native != nullptr) {
            pressed = ImGui::ImageButton(strId, static_cast<ImTextureID>(reinterpret_cast<intptr_t>(native)),
                                         ImVec2(size, size), ImVec2(sprite.u0, sprite.v0), ImVec2(sprite.u1, sprite.v1),
                                         ImVec4(0.0f, 0.0f, 0.0f, 0.0f), iconTint(icons, id));
            unstyle();
            if (tip != nullptr)
                ImGui::SetItemTooltip("%s", tip);
            return pressed;
        }
    }
    pressed = ImGui::Button(word);
    unstyle();
    if (tip != nullptr)
        ImGui::SetItemTooltip("%s", tip);
    return pressed;
}

// The icon for an instance, which is mechanical: the class's own name with a
// prefix. A class this build's theme has never heard of falls back, which is
// what makes a project's own class draw as a generic instance rather than as a
// hole.
[[nodiscard]] std::string classIconId(std::string_view className)
{
    return "class." + std::string(className);
}

[[nodiscard]] std::string classIconId(const scene::World& world, core::InstanceId id)
{
    const scene::ClassDescriptor* descriptor = world.classes().find(world.classOf(id));
    if (descriptor == nullptr)
        return std::string(icons::ClassInstance);
    return classIconId(world.atoms().text(descriptor->name));
}

// One step of the content browser's path, as a control that looks like the text
// it is.
//
// **A breadcrumb is not a row of buttons.** `SmallButton` draws a filled,
// bordered box, which was invisible while the shell drew neither and became a
// rectangle around every folder name the moment a theme did. What a path wants
// is words you can click, so the frame goes and the affordance is what every
// file manager uses instead: the cursor changes and the word underlines.
[[nodiscard]] bool crumbButton(const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    const bool pressed = ImGui::SmallButton(label);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 lo = ImGui::GetItemRectMin();
        const ImVec2 hi = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(lo.x, hi.y), ImVec2(hi.x, hi.y), ImGui::GetColorU32(ImGuiCol_Text));
    }
    return pressed;
}

// Case-insensitive substring, for the add menu's filter box. ASCII, because
// every class name in this engine is (R1) and a full Unicode fold would be a
// dependency for a filter over thirty identifiers.
[[nodiscard]] bool containsFold(std::string_view haystack, std::string_view needle) noexcept
{
    if (needle.size() > haystack.size())
        return false;
    const auto lower = [](char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        std::size_t index = 0;
        while (index < needle.size() && lower(haystack[start + index]) == lower(needle[index]))
            ++index;
        if (index == needle.size())
            return true;
    }
    return false;
}

// Reused across frames rather than rebuilt: the panel fills these once per
// frame for as long as it is open, and a debug overlay that allocates a whole
// tree every frame is a profile artefact somebody eventually has to explain.
// The rows a person could actually see if they scrolled: everything not under a
// closed ancestor. Filled before anything is drawn, because a clipper needs to
// know how many rows exist before it decides which to draw -- and filled by a
// walk that never enters a closed subtree at all, so this is the only list the
// panel builds and its length is what is open rather than what exists.
std::vector<TreeRow> g_visible;
// The whole tree and what a search kept of it. Only filled while something is
// typed into the Explorer's search box -- see the block that uses them.
std::vector<TreeRow> g_searchRows;
std::unordered_set<core::u32> g_searchHits;
// The way down to something just created, moved or copied. See the reveal below.
std::vector<core::InstanceId> g_ancestors;
// Which instances are expanded, and which have been seen at all. Held here
// rather than in ImGui's own tree state, because a clipped row's widget never
// runs and therefore has no state to hold.
std::unordered_set<core::u32> g_open;
std::unordered_set<core::u32> g_openKnown;
// Which world the two sets above describe. They are keyed by instance INDEX and
// indices are recycled, so carrying them across a world would let a new instance
// inherit whether a dead one was expanded.
core::u64 g_explorerWorld = 0;
// The classes the add menu offers, and the world generation they were collected
// for. The registry does not change inside a world, so this is filled once and
// re-read; a fresh world refills it because a reload can register a different
// set (a project's own classes, a shipping build with no `DevOnly`).
std::vector<scene::ClassId> g_creatable;
core::u64 g_creatableWorld = 0;
// What the add menu is filtering by, and which row opened it. The filter is
// per-popup rather than global: it is a way through a list of thirty, not a
// setting anybody wants remembered.
std::array<char, 64> g_addFilter{};
// The instance index whose add popup is open, or zero. Slot zero is never an
// instance, which is what makes it usable as "none".
core::u32 g_addOpenRow = 0;
// Whether the right-drag in progress BEGAN over the viewport image. Latched on
// the press, because that is the only moment the question can be answered: once
// the pointer is held it stops reporting a position, and asking afterwards lets
// a right-click in any panel become a camera turn.
bool g_lookLatched = false;
std::vector<const scene::PropertyDesc*> g_properties;

// The drag-and-drop type the Explorer's rows publish and accept. ImGui matches
// payloads by this string, so it is one constant rather than a literal at two
// call sites: a typo in either is a drop that silently never happens, with
// nothing anywhere saying why.
constexpr const char* kInstanceDragPayload = "luaug.instances";

// What a dragged Explorer row carries.
struct InstanceDrag
{
    core::InstanceId id;
};

// The drag-and-drop type an entry of the content browser publishes, and what it
// carries: a content-relative path.
//
// **A path and not an index**, for the reason the content actions already
// resolve by path: the browser may have re-read its folder between the drag
// starting and the drop landing, and an index into a list that moved is how a
// drop lands on the row below the one somebody grabbed.
//
// Fixed-size, because an ImGui payload is bytes it copies -- a pointer into a
// string the browser owns would dangle the moment that folder is re-read.
constexpr const char* kContentDragPayload = "luaug.content";
struct ContentDrag
{
    char path[240]{};
};

// The instance tree, virtualised.
//
// **Every row used to be drawn every frame**, which on the flagship is 4,300
// `TreeNodeEx` calls sixty times a second for a panel showing thirty of them.
// The content browser beside this one clips, and applying that standard to one
// panel and not the other was an inconsistency rather than a decision.
//
// Clipping a TREE is not clipping a list, and the difference is the reason this
// is longer than it was. A clipper skips rows, and `TreeNodeEx` keeps its own
// open/closed state -- so a row nobody drew has no state, and its children would
// not know whether to appear. So the open set is held HERE, keyed by instance,
// the visible rows are computed before anything is drawn, and the clipper runs
// over that flat list. Which is what a virtualised tree view is.
// `commands` and `dialogs` are null in the F3 overlay, and that is the
// distinction rather than a convenience: **the overlay inspects and the editor
// edits.** Offering Delete over a running game would be offering an edit whose
// only result is a world nobody can put back.
// The colour menu both panels open on a folder.
//
// **Presets first and a picker underneath**, which is the order of how often
// each is used: ten swatches cover almost every case in one click, and the
// picker is there so the palette is a shortcut rather than a limit. "None"
// comes last because taking a colour off is the rarest of the three and
// putting it where a preset would be is how somebody clears one by accident.
//
// Returns true when `color` changed, so the caller can record one undo step for
// it rather than one per frame the picker is open.
[[nodiscard]] bool colorMenu(std::optional<core::Color3>& color)
{
    bool changed = false;

    const std::span<const core::Color3> palette = Editor::folderPalette();
    const float swatch = ImGui::GetFrameHeight();
    for (std::size_t index = 0; index < palette.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        const core::Color3 candidate = palette[index];
        const ImVec4 rgba(candidate.r, candidate.g, candidate.b, 1.0f);
        if (ImGui::ColorButton("##swatch", rgba, ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch))) {
            color = candidate;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
        // Five and five rather than one row of ten, because a menu as wide as
        // ten swatches is wider than every other item in it.
        if (index % 5 != 4)
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }

    ImGui::Separator();

    // Seeded with what is already there, so opening the picker on a coloured
    // folder starts from its colour rather than from black.
    float components[3]{0.6f, 0.62f, 0.66f};
    if (color.has_value()) {
        components[0] = color->r;
        components[1] = color->g;
        components[2] = color->b;
    }
    ImGui::SetNextItemWidth(swatch * 5.0f + ImGui::GetStyle().ItemInnerSpacing.x * 4.0f);
    if (ImGui::ColorEdit3("##pick", components, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        color = core::Color3{components[0], components[1], components[2]};
        changed = true;
    }

    if (color.has_value() && ImGui::MenuItem("None")) {
        color.reset();
        changed = true;
    }
    return changed;
}

// The tree, from `root` down.
//
// **`root` is not drawn when it is the world's**, and IS drawn when it is a
// stamp's. Those are two different questions with the same shape. `game` has no
// properties worth a row, cannot be renamed, deleted, duplicated or reparented,
// and every useful thing is under it -- so a row for it is a line of chrome and
// an indent charged to every row beneath. A STAMP's root is the opposite: it is
// the thing being edited, it is the one instance whose name and transform are
// its own, and hiding it would leave somebody editing a set of children with
// nothing saying what they are children OF.
void drawExplorer(scene::World& world, core::InstanceId root, Inspector& inspector, EditorCommands* commands,
                  EditorDialogs* dialogs, const IconAtlas* icons, bool showGenerated, bool drawRoot = false,
                  bool hasClipboard = false)
{
    // How much of the depth is above the first row that gets drawn. One when
    // the root is hidden, zero when it is not -- and every position on a row is
    // measured from it.
    const u32 depthBase = drawRoot ? 0u : 1u;

    // **The search box, above the tree it searches.**
    //
    // Held per WORLD, like the expanded set below and for the same reason: a
    // filter that survived a scene load would be hiding most of whatever
    // somebody just opened, with nothing on screen saying why.
    static std::array<char, 96> explorerFilter{};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##explorer-filter", "search", explorerFilter.data(), explorerFilter.size());
    const std::string_view explorerNeedle{explorerFilter.data()};

    // **A DIFFERENT world opens collapsed; a restored one does not.** Loading a
    // scene or starting a new one arrives here as a world nobody has expanded
    // anything in, and a tree that remembered would be showing somebody the
    // shape of the scene they just closed -- worse, slot indices restart, so a
    // new instance would inherit whether a dead one was expanded.
    //
    // A stop, an undo or a redo is not that (D071). `World::restore` carries
    // generations precisely so an id keeps its meaning, so every row this set
    // names is still the row it named -- and an undo that collapsed the whole
    // tree took back more than the edit it was asked to.
    if (g_explorerWorld != inspector.worldIdentity()) {
        g_explorerWorld = inspector.worldIdentity();
        g_open.clear();
        g_openKnown.clear();
        explorerFilter.fill(0);
    }

    // **Something was just made, moved or copied: open the way to it.** Before
    // visibility is decided rather than after, or the row would appear one
    // frame late -- and a person who has just pressed a plus is looking at the
    // tree on this frame.
    //
    // A parent that has never been opened is not opened by gaining a child, and
    // an EMPTY one has no chevron to open it with -- so a `Part` created inside
    // a fresh `Folder` was invisible every time, which reads as "I cannot add a
    // child to a folder". The request is one-shot, so a row deliberately closed
    // afterwards stays closed.
    if (const core::InstanceId wanted = inspector.takeReveal(); wanted.valid()) {
        collectAncestors(world, wanted, root, g_ancestors);
        for (const core::InstanceId ancestor : g_ancestors) {
            g_open.insert(ancestor.index);
            g_openKnown.insert(ancestor.index);
        }
    }

    // **One walk, and it does not enter what nobody can see** (ADR 0054). This
    // was a full preorder over every instance in the world followed by a second
    // pass that dropped the ones under something closed -- so an editor on a
    // world of thirty thousand instances paid for thirty thousand of them every
    // frame to draw the four rows on screen. The drawing was already clipped,
    // which is exactly why nothing ever showed it in a profile: the cost was in
    // deciding what to draw, not in drawing it.
    //
    // Parent before child, so a closed or hidden ancestor is answered once and
    // its whole subtree is never touched.
    // **Searching a TREE is not filtering a list**, and the difference is what
    // this block is. A row that matches is no use on its own: everything above
    // it has to be on screen or the match is unreachable, and everything under a
    // matching container is what somebody was looking FOR when they typed the
    // container's name. So the set is the matches, their ancestors, and their
    // descendants -- and while it is non-empty the open/closed state is ignored,
    // because a person searching has already said what they want to see.
    //
    // **And this one walk IS the whole world**, which is the cost the panel
    // otherwise refuses to pay (ADR 0054). It is paid only while something is
    // typed: a search is a thing somebody is doing, and the frame after they
    // clear the box the panel is back to costing what is open.
    if (!explorerNeedle.empty()) {
        collectTree(world, root, g_searchRows);

        g_searchHits.clear();
        // Preorder, so `depth` alone says which run of rows is under a match:
        // everything deeper than a hit, until a row at the hit's depth or above.
        u32 keepBelow = std::numeric_limits<u32>::max();
        for (const TreeRow& row : g_searchRows) {
            if (row.depth <= keepBelow)
                keepBelow = std::numeric_limits<u32>::max();
            if (keepBelow != std::numeric_limits<u32>::max()) {
                g_searchHits.insert(row.id.index);
                continue;
            }
            if (!showGenerated && world.generated(row.id))
                continue;
            if (containsFold(world.atoms().text(world.name(row.id)), explorerNeedle)) {
                g_searchHits.insert(row.id.index);
                keepBelow = row.depth;
            }
        }

        // The way up from every hit, so nothing on screen is an orphan.
        for (const TreeRow& row : g_searchRows) {
            if (!g_searchHits.contains(row.id.index))
                continue;
            for (core::InstanceId up = world.parentOf(row.id); up.valid() && up != root; up = world.parentOf(up))
                g_searchHits.insert(up.index);
        }

        g_visible.clear();
        for (const TreeRow& row : g_searchRows) {
            if (!showGenerated && world.generated(row.id))
                continue;
            if ((row.depth > 0 || drawRoot) && g_searchHits.contains(row.id.index))
                g_visible.push_back(row);
        }
    }
    else
        collectVisibleTree(
            world, root, drawRoot,
            [&](const TreeRow& row) {
                // **What streaming made is not the scene.** It pumps in edit mode as
                // well as in play -- deliberately, because a world you cannot see is
                // a world you cannot edit -- but the serializer skips a generated
                // subtree whole and nothing authored may live in one, so sixty
                // `Chunk_x_y_z` folders standing between a person and the four
                // things they wrote is the root's own complaint again: scrolling
                // past a world to find the thing you came for. Window > Streamed
                // Content brings them back.
                if (!showGenerated && world.generated(row.id))
                    return TreeVisit::Skip;

                const bool hasChildren = world.childCount(row.id) > 0;
                if (!hasChildren)
                    return TreeVisit::Collapsed;

                // The services under `game` are what anyone opening this wants to
                // see; deeper than that is a project's own tree and is its business.
                // Seeded once per instance rather than every frame, so collapsing
                // one stays collapsed.
                if (!g_openKnown.contains(row.id.index)) {
                    g_openKnown.insert(row.id.index);
                    // **The root only**, and when it is the world's it is not drawn
                    // -- opening it is what puts the services on screen at all. What
                    // is INSIDE them is the scene, and showing all of that means
                    // scrolling past a world to find the thing you came for.
                    //
                    // A stamp's root opens for the opposite reason: it IS what
                    // somebody opened, and a stage that starts collapsed shows one
                    // row.
                    if (row.depth == 0)
                        g_open.insert(row.id.index);
                }
                return g_open.contains(row.id.index) ? TreeVisit::Expanded : TreeVisit::Collapsed;
            },
            g_visible);

    // **One height for a row, decided once and told to everybody.**
    //
    // A tree row held three things of three different heights -- a framed
    // arrow, a square icon and a line of text -- and each of the three decided
    // where it sat. Worse, `ImGuiListClipper` was left to assume a row pitch of
    // its own, which is `GetTextLineHeightWithSpacing()` and is not what a
    // framed arrow makes a row: the selection highlight came out taller than
    // the pitch the clipper had reserved, so it covered the row above and the
    // row below, and nothing on the row lined up with anything else on it.
    //
    // So the height is a number: the frame height, which is the font's line
    // plus the theme's padding and therefore scales with both -- responsive
    // without being a magic constant. The clipper is told it, the selectable is
    // built to it, everything drawn on the row is centred against it, and the
    // cursor is placed at exactly the next multiple of it. Four agreements
    // instead of four guesses.
    const float indentSpacing = ImGui::GetStyle().IndentSpacing;
    const float rowHeight = ImGui::GetFrameHeight();

    // **And no vertical item spacing while the rows are drawn.** A `Selectable`
    // pads its highlight with HALF of `ItemSpacing.y` above and below -- which
    // is what makes a column of them read as one continuous list, and which is
    // exactly wrong once the rows are laid out at an exact pitch: the padding
    // has nowhere to go but into the row above and the row below. Zero here
    // makes the highlight the row, and every position on the row is explicit
    // anyway, so nothing else in this loop notices.
    const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing.x, 0.0f));

    // Where row zero starts, in screen space. **The exact pitch pays for itself
    // here**: with it, row `i` is at `listTop + i * rowHeight` and the guide
    // lines below can be drawn for rows the clipper never visited -- which is
    // what makes a line reaching a child scrolled half off the bottom still
    // reach it.
    const ImVec2 listTop = ImGui::GetCursorScreenPos();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(g_visible.size()), rowHeight);
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const TreeRow& row = g_visible[static_cast<std::size_t>(index)];
            const bool hasChildren = world.childCount(row.id) > 0;

            ImGui::PushID(static_cast<int>(row.id.index));

            const ImVec2 rowOrigin = ImGui::GetCursorPos();
            const float iconSize = ImGui::GetTextLineHeight();
            // Never above the row's top, whatever it is handed. An element
            // taller than the row is a mistake, and starting it half a
            // padding above the row is that mistake spread over two rows.
            const auto centred = [&](float height) {
                const float slack = rowHeight - height;
                return rowOrigin.y + (slack > 0.0f ? slack * 0.5f : 0.0f);
            };

            const std::string_view instanceName = world.atoms().text(world.name(row.id));
            const scene::ClassDescriptor* classDescriptor = world.classes().find(world.classOf(row.id));
            const std::string_view className =
                classDescriptor != nullptr ? world.atoms().text(classDescriptor->name) : std::string_view("?");

            char label[192];
            const bool haveIcon = icons != nullptr && icons->ready();
            if (haveIcon) {
                // **The class is not in the text, because the icon IS the
                // class** -- that is what a `class.<ClassName>` id means, and
                // saying it twice costs width the name needs. Without an atlas
                // it comes back, because a row showing neither has lost the
                // answer rather than moved it.
                (void)std::snprintf(label, sizeof(label), "%.*s", static_cast<int>(instanceName.size()),
                                    instanceName.data());
            }
            else {
                (void)std::snprintf(label, sizeof(label), "%.*s  (%.*s)", static_cast<int>(instanceName.size()),
                                    instanceName.data(), static_cast<int>(className.size()), className.data());
            }

            // **The row itself is drawn first and spans the full width**,
            // arrow included and indent ignored. It owns the hit test, the
            // highlight and the height; the arrow, the icon, the name and the
            // plus are placed on top of it afterwards, which is also what puts
            // them OVER the highlight rather than under it.
            if (ImGui::Selectable("##row", inspector.isSelected(row.id), ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0.0f, rowHeight))) {
                // Ctrl adds and removes, shift takes the run from the primary
                // to here, a plain click replaces. The range is taken over
                // `g_visible` rather than over the whole tree, because a range
                // somebody drew with the mouse across a collapsed branch would
                // pick up rows they cannot see.
                const ImGuiIO& io = ImGui::GetIO();
                if (io.KeyCtrl) {
                    inspector.toggle(row.id);
                }
                else if (io.KeyShift && inspector.selection().valid()) {
                    selectVisibleRange(inspector, g_visible, inspector.selection(), row.id);
                }
                else {
                    inspector.select(row.id);
                }
            }
            // Asked while the selectable is still the last item, which is the
            // only moment it answers about the ROW rather than about the button
            // drawn on top of it.
            const bool rowHovered = ImGui::IsItemHovered();
            // The popup keeps the plus drawn while it is open, or the button
            // vanishes the moment the pointer leaves the row to reach the list
            // and takes its own popup with it.
            const bool addOpen = g_addOpenRow == row.id.index;

            // --- Moving something by dragging it ---------------------------
            //
            // **Both halves live on the SAME item as the selection**, and they
            // have to: ImGui reads the last item for the source's id and for
            // the target's rectangle, so a drag source attached to anything
            // other than the row's own selectable drags the wrong thing.
            //
            // The drop target is asked first. `BeginDragDropSource` returns in
            // three lines unless this row is the one being held, but on the row
            // that IS held it opens a tooltip window, and a target asked
            // afterwards would be reading that window's last item rather than
            // the row's.
            if (commands != nullptr && ImGui::BeginDragDropTarget()) {
                // **Two drops, told apart by what is being dragged.**
                //
                // A row of this tree is a reparent, and the row lights only
                // where the move would do something -- a row that highlights and
                // then refuses is the same broken promise a field that takes a
                // drag the world rejects makes, so the rule is asked here, a
                // frame early, through the one function the verb itself uses.
                //
                // An entry of the content browser is a PREFAB, and dropping one
                // places it under this row. Anywhere authorable takes one.
                const ImGuiPayload* peek = ImGui::GetDragDropPayload();
                const bool fromTree = peek != nullptr && peek->IsDataType(kInstanceDragPayload);
                if (fromTree && Editor::canReparent(world, inspector.selectionSet(), row.id, root) &&
                    ImGui::AcceptDragDropPayload(kInstanceDragPayload) != nullptr) {
                    commands->reparentTo = row.id;
                }

                // **A prefab dropped from the browser lands HERE**, under the
                // row it was dropped on rather than wherever the selection
                // happens to be -- which is the whole reason a drop is worth
                // having beside the menu item that does the same thing.
                const bool fromBrowser = peek != nullptr && peek->IsDataType(kContentDragPayload);
                if (fromBrowser && Editor::canParentInto(world, row.id, root)) {
                    if (const ImGuiPayload* took = ImGui::AcceptDragDropPayload(kContentDragPayload); took != nullptr) {
                        commands->placeStamp = static_cast<const ContentDrag*>(took->Data)->path;
                        commands->placeStampLinked = true;
                        commands->placeStampParent = row.id;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (commands != nullptr && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                // The same rule the right-click follows, and for the same
                // reason: a drag that started on a row nobody had selected acts
                // on that row, and one that started on a member of the
                // selection takes all of it. Anything else throws away four
                // things somebody had just picked in order to move one.
                if (!inspector.isSelected(row.id))
                    inspector.select(row.id);

                // **The payload says WHERE FROM, and that is all it has to
                // say.** Within one tree what moves is the selection, read at
                // the drain like the delete and the duplicate; across two it is
                // this one instance, because a selection cannot span worlds.
                const InstanceDrag dragged{row.id};
                ImGui::SetDragDropPayload(kInstanceDragPayload, &dragged, sizeof(dragged));

                const core::usize dragging = inspector.selectionCount();
                if (dragging > 1)
                    ImGui::Text("%d instances", static_cast<int>(dragging));
                else
                    ImGui::TextUnformatted(label);
                ImGui::EndDragDropSource();
            }

            // **Right-clicking selects first, unless this row is already part
            // of the selection.** A menu that acted on whatever was selected
            // before would delete the wrong thing the first time somebody
            // right-clicked without looking -- and one that replaced the
            // selection would throw away the four things they had just picked
            // in order to act on one of them.
            if (commands != nullptr && dialogs != nullptr && ImGui::BeginPopupContextItem("row-menu")) {
                // **The row's spacing is the ROW's, not this menu's.** Style
                // vars are a global stack, so the zero the rows are drawn with
                // reaches every window opened while they are -- and a menu whose
                // items have no space between them is the row fix leaking into
                // something that never asked for it.
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);
                if (!inspector.isSelected(row.id))
                    inspector.select(row.id);

                const bool engineOwned = Editor::isEngineOwned(world, row.id, root);

                // **Offered on anything that has children**, rather than on the
                // `Folder` class alone. A `Model` full of parts is a folder in
                // every way that matters to somebody scanning a tree, and a
                // rule that asked the class would colour one and not the other
                // for no reason a person could see.
                if (world.childCount(row.id) > 0 || Editor::folderColor(world, row.id).has_value()) {
                    if (ImGui::BeginMenu("Colour")) {
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);
                        std::optional<core::Color3> chosen = Editor::folderColor(world, row.id);
                        if (colorMenu(chosen)) {
                            commands->colorAsked = true;
                            commands->colorTarget = row.id;
                            commands->colorContentPath.clear();
                            commands->color = chosen;
                        }
                        ImGui::PopStyleVar();
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                }

                // **Bringing a file in from the machine, and getting the
                // instance for it.** The file still lands in `content/` --
                // that is where a project's files live and there is nowhere
                // else for them to go -- and what the Explorer adds is the
                // thing in the world that names it. A file the world has no
                // class for is imported and nothing more, which is honest and
                // is what the status line then says.
                if (ImGui::MenuItem("Import...", nullptr, false, platform::canPickFolder())) {
                    commands->importAssets = true;
                    commands->importParent = row.id;
                }
                ImGui::Separator();

                if (ImGui::MenuItem("Rename...", "F2", false, !engineOwned)) {
                    dialogs->renameTarget = row.id;
                    dialogs->renameContentPath.clear();
                    dialogs->renameSeed = std::string(instanceName);
                    dialogs->renameInstance = true;
                }
                // Greyed rather than refused afterwards. The rule lives in
                // `Editor` because it is a rule about the world; the menu
                // reflects it so nobody presses a thing that cannot happen.
                // **On the SELECTION**, which the right-click above has just
                // made sure this row is part of. Labelled with the count when
                // there is more than one, because "Delete" over four things
                // should say four.
                const core::usize count = inspector.selectionCount();
                char duplicateLabel[48];
                char deleteLabel[48];
                (void)std::snprintf(duplicateLabel, sizeof(duplicateLabel), count > 1 ? "Duplicate %d" : "Duplicate",
                                    static_cast<int>(count));
                (void)std::snprintf(deleteLabel, sizeof(deleteLabel), count > 1 ? "Delete %d" : "Delete",
                                    static_cast<int>(count));
                if (ImGui::MenuItem(duplicateLabel, "Ctrl+D", false, !engineOwned))
                    commands->duplicateSelection = true;

                // **Copy, cut and the two pastes**, greyed rather than refused
                // afterwards -- which is the rule every other item in this menu
                // follows, and the reason `hasClipboard` is passed in at all.
                ImGui::Separator();
                if (ImGui::MenuItem("Copy", "Ctrl+C", false, !engineOwned))
                    commands->copySelection = true;
                if (ImGui::MenuItem("Cut", "Ctrl+X", false, !engineOwned))
                    commands->cutSelection = true;
                // Beside this one, and inside it. That is the whole difference,
                // and it is why they are two items rather than one.
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, hasClipboard))
                    commands->paste = true;
                if (ImGui::MenuItem("Paste Into", "Ctrl+Shift+V", false, hasClipboard))
                    commands->pasteInto = true;

                // --- Stamps (ADR 0049) --------------------------------------
                //
                // On the row rather than in a menu bar, because "a stamp of
                // WHAT" is the whole question and the row is the answer.
                ImGui::Separator();
                const core::InstanceId stampRoot = world.stampRootOf(row.id);
                if (stampRoot.valid()) {
                    const std::string_view stampName = world.atoms().text(world.stampOf(stampRoot));
                    char stampLabel[160];
                    (void)std::snprintf(stampLabel, sizeof(stampLabel), "Break Stamp (%.*s)",
                                        static_cast<int>(stampName.size()), stampName.data());
                    if (ImGui::MenuItem(stampLabel))
                        commands->breakStamp = stampRoot;
                }
                else if (ImGui::MenuItem("Convert to Stamp...", nullptr, false, !engineOwned)) {
                    dialogs->stampSubject = row.id;
                    dialogs->newStamp = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(deleteLabel, "Del", false, !engineOwned))
                    commands->deleteSelection = true;
                ImGui::PopStyleVar();
                ImGui::EndPopup();
            }

            // --- What the row shows, placed on it and centred against it ----
            //
            // Every one of these is positioned outright rather than stacked
            // after the last: they are four different heights, and stacking
            // would put them at four different places on a row that has one.

            // Less whatever is above the first drawn row: with the world's
            // root hidden its children are this tree's top level, and with a
            // stamp's root drawn the root itself is.
            float penX = rowOrigin.x + static_cast<float>(row.depth - depthBase) * indentSpacing;

            // The arrow is its own control rather than part of the row, because
            // opening a thing and selecting it are different intentions and a
            // tree that conflates them makes browsing destructive.
            //
            // **The set's own chevrons rather than ImGui's**, and the pairing is
            // what the row can DO rather than what it is: an open row offers to
            // collapse, a closed one offers to expand.
            if (hasChildren) {
                const bool open = g_open.contains(row.id.index);
                // Frameless, so the button IS the picture and centring by the
                // face cannot be wrong -- `buttonFace` exists for the framed
                // case and this one no longer has a frame.
                ImGui::SetCursorPos(ImVec2(penX, centred(iconSize)));
                const bool toggled = haveIcon ? iconButton(icons, open ? icons::ActionCollapse : icons::ActionExpand,
                                                           iconSize, "toggle", open ? "-" : "+", nullptr, true)
                                              : ImGui::ArrowButton("toggle", open ? ImGuiDir_Down : ImGuiDir_Right);
                if (toggled) {
                    if (!g_open.insert(row.id.index).second)
                        g_open.erase(row.id.index);
                }
            }
            // The space is reserved either way, so a leaf's icon lines up with
            // its siblings' rather than sliding left to where their arrow is --
            // and it is reserved at the button's WIDTH, which is the face plus
            // the horizontal padding and is not the row height.
            penX += iconSize + ImGui::GetStyle().ItemInnerSpacing.x;

            // **The plus, at the end of the row's text.** Making a child of the
            // thing you are looking at is the commonest authoring act there is,
            // and until this the only way to add an instance to this engine at
            // all was to write `Instance.new` in a script. It sits on the ROW
            // rather than in the toolbar because which parent is the whole of
            // the question, and after the name rather than before it because
            // before it is where the name goes.
            if (haveIcon) {
                ImGui::SetCursorPos(ImVec2(penX, centred(iconSize)));
                // Screen space, taken before the draw: the badge below is not
                // an ImGui item and has to be placed against the icon's own box
                // rather than against whatever the cursor is after it.
                const ImVec2 iconOrigin = ImGui::GetCursorScreenPos();
                if (drawIcon(icons, classIconId(world, row.id), iconSize, Editor::folderColor(world, row.id))) {
                    // **The badge goes on the stamp's ROOT and nowhere else.**
                    // Every part of a stamped house is inside a stamped
                    // subtree, and badging all forty of them is noise -- the
                    // root is where the mark lives (ADR 0049) and where "is
                    // this linked?" is a question worth answering.
                    const core::NameAtom stamp = world.stampOf(row.id);
                    if (stamp.valid())
                        drawIconBadge(icons, iconOrigin, iconSize);

                    if (ImGui::IsItemHovered()) {
                        if (stamp.valid()) {
                            const std::string_view file = world.atoms().text(stamp);
                            ImGui::SetTooltip("%.*s -- stamped from content/%.*s", static_cast<int>(className.size()),
                                              className.data(), static_cast<int>(file.size()), file.data());
                        }
                        else {
                            ImGui::SetTooltip("%.*s", static_cast<int>(className.size()), className.data());
                        }
                    }
                    penX += iconSize + ImGui::GetStyle().ItemInnerSpacing.x;
                }
            }

            ImGui::SetCursorPos(ImVec2(penX, centred(ImGui::GetTextLineHeight())));
            ImGui::TextUnformatted(label);
            penX += ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x;

            // Only where an instance can actually go: nothing authored lives
            // inside something streaming materialised, and offering a plus that
            // refuses is worse than not offering one.
            //
            // **Asked of the ANCESTRY**, through the same function the verb
            // uses. Asking `generated` of the row alone got the chunk right and
            // everything inside the chunk wrong -- `Chunk_-3_0_0/Ground` is not
            // itself generated, so it wore a plus, took the create, and lost it
            // at the next eviction.
            if (commands != nullptr && Editor::canParentInto(world, row.id, root)) {
                // **Shown on the row under the pointer and on the selected
                // ones -- and EXISTING on all of them.** The two are different
                // questions and conflating them is what broke the first click.
                //
                // ImGui resolves an overlap in favour of the LATER item, but
                // only if the later item was there when the overlap happened. A
                // button that comes into being on the same frame the row becomes
                // hovered is a button whose press the selectable underneath has
                // already taken -- so the first click selected the row and only
                // the second opened the menu, which is what somebody trying to
                // add a child to `Workspace` runs into.
                //
                // So the item is emitted every frame and drawn at zero alpha
                // when it is not wanted. Alpha is a rendering property and not
                // a hit-testing one, so the press lands the first time; and the
                // only rows where it is invisible are rows the pointer is not
                // on, which are the rows nobody can click it on anyway. A
                // thousand-row tree shows no plus signs at all.
                ImGui::SetCursorPos(ImVec2(penX, centred(iconSize)));

                // **Under the pointer, and nowhere else.** It was drawn on the
                // selected rows too, which meant four selected rows carried
                // four plus signs while somebody was reading them -- and the
                // one they can press is the one their pointer is already on.
                // The popup keeps it drawn while it is open, or the button
                // vanishes the moment the pointer leaves the row to reach the
                // list and takes its own popup with it.
                const bool lit = rowHovered || addOpen;
                if (!lit)
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
                const bool add =
                    haveIcon ? iconButton(icons, icons::ActionAdd, iconSize, "add", "+", "add a child instance", true)
                             : ImGui::SmallButton("+");
                if (!lit)
                    ImGui::PopStyleVar();
                if (add)
                    ImGui::OpenPopup("add-child");
            }

            // **The next row starts exactly one row height down**, whatever the
            // last thing drawn on this one happened to be. Without this the
            // pitch is whatever ImGui's line tracking made of four items placed
            // by hand, which is neither `rowHeight` nor what the clipper was
            // told -- and a pitch that disagrees with the clipper is a tree
            // that scrolls to the wrong place.
            ImGui::SetCursorPos(ImVec2(rowOrigin.x, rowOrigin.y + rowHeight));

            if (commands != nullptr && ImGui::BeginPopup("add-child")) {
                // Same as the row menu above: this window is not a row.
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);
                g_addOpenRow = row.id.index;
                if (g_creatableWorld != inspector.worldIdentity() || g_creatable.empty()) {
                    g_creatableWorld = inspector.worldIdentity();
                    collectCreatableClasses(world, g_creatable);
                }

                // Focused on open, because a list of thirty is a list you type
                // at rather than scroll -- and the first keystroke landing in
                // the box is what makes that true.
                if (ImGui::IsWindowAppearing()) {
                    g_addFilter.fill(0);
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::SetNextItemWidth(210.0f);
                // "search" rather than "filter": one is what a person is doing
                // and the other is what the code is doing, and a hint is written
                // for the first of those. It is also the word on every other box
                // in this shell now, and three names for one gesture is three
                // things to learn.
                ImGui::InputTextWithHint("##add-filter", "search", g_addFilter.data(), g_addFilter.size());

                // **Escape closes it**, which is what Escape does to every
                // transient thing on a screen. The shell's own Escape handler
                // deliberately refuses while a popup is open -- it would take
                // the key from the dialogs that need it -- so the popup that
                // wants it has to ask, and this is the one that does.
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    ImGui::CloseCurrentPopup();

                const std::string_view filter(g_addFilter.data());
                if (ImGui::BeginChild("add-list", ImVec2(210.0f, 260.0f))) {
                    for (const scene::ClassId classId : g_creatable) {
                        const scene::ClassDescriptor* candidate = world.classes().find(classId);
                        if (candidate == nullptr)
                            continue;
                        const std::string_view candidateName = world.atoms().text(candidate->name);
                        if (!filter.empty() && !containsFold(candidateName, filter))
                            continue;

                        // **The icon a row of this class would wear**, drawn
                        // beside the name here for the reason it is drawn in the
                        // tree at all: a list of thirty identifiers is read by
                        // shape before it is read by word, and a menu that names
                        // what it will make without showing it is a menu you have
                        // to read twice.
                        //
                        // The name is still what the item IS -- the selectable
                        // spans the row and the icon is drawn over it -- so
                        // filtering, keyboard focus and the click target are
                        // exactly what they were.
                        char item[96];
                        (void)std::snprintf(item, sizeof(item), "##%.*s", static_cast<int>(candidateName.size()),
                                            candidateName.data());
                        const ImVec2 itemOrigin = ImGui::GetCursorPos();
                        const bool chosen = ImGui::Selectable(item);
                        // Taken here, because the icon and the name are drawn
                        // over the selectable afterwards and `IsItemHovered`
                        // answers about the LAST item -- which would make the
                        // description below appear only over the word.
                        const bool itemHovered = ImGui::IsItemHovered();

                        const float itemIcon = ImGui::GetTextLineHeight();
                        ImGui::SetCursorPos(itemOrigin);
                        // The ATLAS falls back for a class no theme has heard
                        // of -- `debug_overlay_tests.cpp` holds it to that -- so
                        // there is nothing to do here for a project's own class.
                        // False means there is no atlas at all, which is a build
                        // with no icons rather than a class with none, and then
                        // the name simply stands where it always did.
                        if (drawIcon(icons, classIconId(candidateName), itemIcon))
                            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                        else
                            ImGui::SetCursorPos(itemOrigin);
                        ImGui::TextUnformatted(candidateName.data(), candidateName.data() + candidateName.size());

                        if (chosen) {
                            commands->createClass = classId;
                            commands->createParent = row.id;
                            ImGui::CloseCurrentPopup();
                        }
                        // The IDL's own prose, which the properties grid already
                        // shows for a property and which is the only description
                        // of a class anywhere at runtime.
                        if (candidate->doc[0] != 0 && itemHovered) {
                            ImGui::BeginTooltip();
                            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                            ImGui::TextUnformatted(candidate->doc);
                            ImGui::PopTextWrapPos();
                            ImGui::EndTooltip();
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::EndPopup();
            }
            else if (g_addOpenRow == row.id.index) {
                g_addOpenRow = 0;
            }

            ImGui::PopID();
        }
    }

    // --- The guide lines --------------------------------------------------
    //
    // A vertical line under every open parent, running down past its children,
    // with a stub reaching each of them. Depth alone tells you a row is nested;
    // it does not tell you WHICH row it is nested under, and on a tree four
    // levels deep with twenty siblings that is the question somebody actually
    // has.
    //
    // Drawn after the rows so the lines sit over the selection highlight rather
    // than under it -- a highlighted row is exactly the row whose parentage is
    // being read. Drawn from `g_visible` rather than from what the clipper
    // showed, because a parent above the fold still owns the children below it.
    //
    // The last child ends the line at its own middle instead of its bottom,
    // which is what turns a bar into an elbow and says "and no more after this".
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 guide = ImGui::GetColorU32(ImGuiCol_Text, 0.28f);
        const float half = rowHeight * 0.5f;
        // The same face the rows' chevrons are drawn at, so the line hangs from
        // the middle of the arrow rather than near it.
        const float chevron = ImGui::GetTextLineHeight();

        for (std::size_t i = 0; i < g_visible.size(); ++i) {
            const core::u32 depth = g_visible[i].depth;

            // The last visible row still under this one. A run rather than a
            // search: `g_visible` is preorder, so the descendants of a row are
            // exactly the rows after it until the depth stops being greater.
            std::size_t last = i;
            for (std::size_t j = i + 1; j < g_visible.size() && g_visible[j].depth > depth; ++j)
                last = j;
            if (last == i)
                continue;

            // The column: the middle of this row's own chevron, which is where
            // a child's line should hang from.
            const float x =
                std::floor(listTop.x + static_cast<float>(depth - depthBase) * indentSpacing + chevron * 0.5f);
            const float top = listTop.y + static_cast<float>(i) * rowHeight + rowHeight;
            const float bottom = listTop.y + static_cast<float>(last) * rowHeight + half;
            draw->AddLine(ImVec2(x, top), ImVec2(x, bottom), guide);

            // A stub to each DIRECT child, at the child's own middle. Only the
            // direct ones: a line to a grandchild would cross the rows between
            // them and say something that is not true.
            for (std::size_t j = i + 1; j <= last; ++j) {
                if (g_visible[j].depth != depth + 1)
                    continue;
                const float y = std::floor(listTop.y + static_cast<float>(j) * rowHeight + half);
                draw->AddLine(ImVec2(x, y), ImVec2(x + indentSpacing * 0.5f, y), guide);
            }
        }
    }

    ImGui::PopStyleVar();
}

// One widget per `ValueType` and no code per class (Decision 16): everything
// this needs arrives in the `PropertyDesc` it is handed.
//
// Nothing here writes. Every branch that accepts an edit enqueues it, and the
// queue drains at the next FrameStart (Decision 15) through
// `World::setProperty` and nothing else (Decision 14).
// A property whose value is SEVERAL numbers that have names of their own.
//
// **These get a row each, and that is the whole fix.** A `UDim` is a scale and
// an offset, a `UDim2` is two of those, and the panel drew them as two or four
// unlabelled boxes in one cell: nothing on the screen said which number was
// which, and there is no convention to fall back on the way `x y z` has one.
// Putting the word inside the box was worse -- a box is for a value, and
// `scale 0.000` is a box with a caption in it.
//
// So the name goes in the name column, where every other name in this panel
// already is, and the box holds a number and nothing else. That is what every
// properties grid does with a composite value, and it costs no width: the rows
// were always there, they were just all crammed into one.
[[nodiscard]] bool compositeKind(EditorKind kind) noexcept
{
    return kind == EditorKind::UDim || kind == EditorKind::UDim2;
}

// One sub-row: an indented name on the left, one number on the right.
[[nodiscard]] bool numberRow(const char* label, float& value, float step, bool mixed, const char* format)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Indent();
    ImGui::TextUnformatted(label);
    ImGui::Unindent();

    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = ImGui::DragFloat("##value", &value, step, 0.0f, 0.0f, mixed ? "--" : format);
    ImGui::PopID();
    return changed;
}

// The sub-rows of one composite, and the value they add up to.
//
// The offset is drawn whole because it IS whole: it is a count of pixels, and
// `formatValue` has printed it with no decimals since M6 -- the two agree on
// purpose, so a value read in the summary row and the same value read in its
// own row are the same string.
[[nodiscard]] bool drawCompositeRows(EditorKind kind, const SharedValue& shared, scene::Value& out)
{
    const bool mixed = shared.state == SharedState::Mixed;

    if (kind == EditorKind::UDim) {
        // The same guard `drawEditor` makes one function up: the declared type
        // says which rows exist and the variant says what is in it, and those
        // disagree whenever a value is absent. `std::get` would throw.
        const core::UDim* held = std::get_if<core::UDim>(&shared.value);
        if (held == nullptr)
            return false;
        core::UDim value = *held;
        bool changed = numberRow("Scale", value.scale, 0.01f, mixed, "%.3f");
        changed = numberRow("Offset", value.offset, 1.0f, mixed, "%.0f") || changed;
        if (changed)
            out = scene::Value{value};
        return changed;
    }

    const core::UDim2* held = std::get_if<core::UDim2>(&shared.value);
    if (held == nullptr)
        return false;
    core::UDim2 value = *held;
    bool changed = numberRow("X Scale", value.x.scale, 0.01f, mixed, "%.3f");
    changed = numberRow("X Offset", value.x.offset, 1.0f, mixed, "%.0f") || changed;
    changed = numberRow("Y Scale", value.y.scale, 0.01f, mixed, "%.3f") || changed;
    changed = numberRow("Y Offset", value.y.offset, 1.0f, mixed, "%.0f") || changed;
    if (changed)
        out = scene::Value{value};
    return changed;
}

// The kind name the IDL wrote into a descriptor, as the browser's own enum.
// Anything unrecognised is `Other`, which lists nothing rather than everything:
// a picker that fell back to the whole project would be a file dialog with extra
// steps, which is the thing `contentKind` exists to avoid.
[[nodiscard]] ContentKind contentKindNamed(std::string_view name) noexcept
{
    if (name == "Mesh")
        return ContentKind::Mesh;
    if (name == "Texture")
        return ContentKind::Texture;
    if (name == "Audio")
        return ContentKind::Audio;
    if (name == "Font")
        return ContentKind::Font;
    return ContentKind::Other;
}

// `tree` is the content browser's, and null wherever there is none -- the F3
// overlay over a running game has an inspector and no project. The `Content`
// editor then keeps its text field and its drop target and offers an empty list,
// which is the honest state rather than a hidden control.
void drawEditor(scene::World& world, Inspector& inspector, std::span<const core::InstanceId> targets,
                const scene::PropertyDesc& descriptor, const SharedValue& shared, ContentTree* tree)
{
    if (shared.state == SharedState::Unreadable) {
        // The class declares the property and the world cannot read it: a null
        // getter. Shown rather than skipped, because a complete view of the
        // descriptor tables is the entire claim this panel makes.
        ImGui::TextUnformatted("<unreadable>");
        return;
    }

    // **Every widget below writes to the WHOLE selection**, and this is the only
    // place that knows how many that is. One instance is the same path with one
    // member, which is what keeps the single-selection behaviour from being a
    // second implementation nobody exercises.
    const auto commit = [&](const scene::Value& value) {
        for (const core::InstanceId target : targets) {
            if (world.alive(target))
                inspector.enqueue(target, descriptor.name, value);
        }
    };

    // The members disagree. The widget still shows something -- the first live
    // one's value, so a drag has somewhere to start -- and each branch below
    // says so in whatever way its widget can: a checkbox has ImGui's own mixed
    // state, a drag hides its number, a text field starts empty. The row's
    // label carries a `(mixed)` tag as well, because two of the twelve widgets
    // (a colour and a matrix) have no honest way to express it themselves.
    //
    // **Editing a mixed field flattens it**, which is what every engine does and
    // the only thing it can mean: a value typed into a field that stands for
    // forty instances is a value for all forty.
    const bool mixed = shared.state == SharedState::Mixed;

    // `editorFor` answers from the DECLARED type, and the variant holds what the
    // property actually has right now. Those disagree whenever a value is absent
    // -- `scene::Value`'s own comment names the two cases, an unset attribute and
    // a nil Instance reference -- and every branch below reaches for its
    // alternative with `std::get`, which throws rather than returning.
    //
    // Guarded here rather than in each branch: there are eight of them, they all
    // have this shape, and an absent value has no editor whatever its type says.
    // Found by a human clicking `go` on `RunService.Parent`, which selects the
    // DataModel -- the one instance in the world whose own Parent is nil -- and
    // took the host down with an uncaught `std::bad_variant_access`.
    if (std::holds_alternative<std::monostate>(shared.value)) {
        ImGui::TextUnformatted(mixed ? "mixed" : "nil");
        return;
    }

    const EditorKind kind = editorFor(descriptor);
    const std::string_view kindName =
        descriptor.contentKind.valid() ? world.atoms().text(descriptor.contentKind) : std::string_view{};
    ImGui::PushID(static_cast<int>(descriptor.name.id));
    ImGui::SetNextItemWidth(-FLT_MIN);

    // Handled before the disabled block, because its one interaction is a
    // selection rather than a write -- following a reference is how you reach
    // an instance the tree has collapsed away.
    if (kind == EditorKind::InstanceRef) {
        const core::InstanceId reference = std::get<core::InstanceId>(shared.value);
        const std::string text = mixed ? std::string("mixed") : formatValue(world, shared.value);
        ImGui::TextUnformatted(text.c_str());
        // No `go` on a mixed reference: there is no one instance to go to, and
        // jumping to whichever member happened to be first would replace the
        // selection with something nobody pointed at.
        if (!mixed && reference.valid() && world.alive(reference)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("go"))
                inspector.select(reference);
        }
        ImGui::PopID();
        return;
    }

    // `readOnly` is honoured HERE and not only by the setter: a field that
    // takes a drag the world then refuses is a UI making a claim it cannot
    // keep, and the refusal arrives a frame later with nothing attaching it to
    // the gesture that caused it.
    //
    // Over a selection, `collectCommonProperties` has already answered with the
    // most restrictive descriptor of the set, so read-only for any member is
    // read-only here.
    const bool locked = !editable(descriptor);
    if (locked)
        ImGui::BeginDisabled();

    switch (kind) {
    case EditorKind::Checkbox: {
        bool value = std::get<bool>(shared.value);
        // The one widget ImGui expresses this natively for, and it draws a
        // filled square rather than a tick or a gap.
        if (mixed)
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        if (ImGui::Checkbox("##value", &value))
            commit(scene::Value{value});
        if (mixed)
            ImGui::PopItemFlag();
        break;
    }
    case EditorKind::Number: {
        f64 value = std::get<f64>(shared.value);
        // A format with no conversion in it is ImGui's own way of saying "do
        // not show the number": the drag still works and a ctrl-click still
        // opens an empty field that parses. Which is exactly right for a value
        // that is nobody's -- the alternative shows one member's number as if
        // it were everyone's.
        if (ImGui::DragScalar("##value", ImGuiDataType_Double, &value, 0.01f, nullptr, nullptr, mixed ? "--" : "%.4f"))
            commit(scene::Value{value});
        break;
    }
    case EditorKind::Text: {
        const std::string& text = std::get<std::string>(shared.value);
        char buffer[256]{};
        if (!mixed) {
            if (text.size() + 1 > sizeof(buffer)) {
                // Editing through a buffer that cannot hold the value would
                // write a truncated string back on the first Enter. Shown, not
                // offered.
                ImGui::TextUnformatted(text.c_str());
                break;
            }
            std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
        }
        // A mixed field starts EMPTY behind a hint rather than pre-filled with
        // one member's string. Pre-filling would make replacing forty names
        // with one look like a correction rather than an overwrite -- and
        // Enter on a field nobody edited would do it.
        const bool entered =
            mixed ? ImGui::InputTextWithHint("##value", "mixed", buffer, sizeof(buffer),
                                             ImGuiInputTextFlags_EnterReturnsTrue)
                  : ImGui::InputText("##value", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue);
        if (entered)
            commit(scene::Value{std::string(buffer)});
        break;
    }
    case EditorKind::Content: {
        // **A URI is a string, and a person is not a URI.** The engine resolves
        // `asset://…` through the mounts and could not care which file a person
        // meant; what a person has is a name they half remember and a browser
        // full of files. So this is three ways in and they are all the same
        // property: type the path, pick it from the ones this property ACCEPTS,
        // or drag the file onto the field.
        const std::string& current = std::get<std::string>(shared.value);
        char buffer[256]{};
        if (!mixed && current.size() + 1 <= sizeof(buffer))
            std::snprintf(buffer, sizeof(buffer), "%s", current.c_str());

        const float pickWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(pickWidth + ImGui::GetStyle().ItemInnerSpacing.x));
        const bool typed = ImGui::InputTextWithHint("##value", mixed ? "mixed" : "asset://", buffer, sizeof(buffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (typed)
            commit(scene::Value{std::string(buffer)});

        // **The field itself takes a drop from the browser.** Dragging a file
        // onto the property it belongs to is the gesture every engine has, and
        // it is the one that does not require knowing what the path is called.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* took = ImGui::AcceptDragDropPayload(kContentDragPayload); took != nullptr) {
                const auto* dragged = static_cast<const ContentDrag*>(took->Data);
                commit(scene::Value{std::string("asset://") + dragged->path});
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button("...", ImVec2(pickWidth, 0.0f)))
            ImGui::OpenPopup("content-pick");

        if (ImGui::BeginPopup("content-pick")) {
            // Read when the popup OPENS rather than every frame: a project's
            // assets are a directory walk, and a list that is rebuilt sixty
            // times a second is one the filter is fighting.
            static std::vector<std::string> candidates;
            static std::array<char, 96> search{};
            if (ImGui::IsWindowAppearing()) {
                candidates =
                    tree != nullptr ? tree->filesOfKind(contentKindNamed(kindName)) : std::vector<std::string>{};
                search.fill(0);
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            ImGui::InputTextWithHint("##content-search", "search", search.data(), search.size());
            const std::string_view needle{search.data()};

            ImGui::Separator();
            if (ImGui::BeginChild("content-list", ImVec2(ImGui::GetFontSize() * 16.0f, ImGui::GetFontSize() * 12.0f))) {
                // Clearing is a real answer and the first one offered: a
                // property that names nothing is a legal property, and hunting
                // for the way to say so is worse than an extra row.
                if (ImGui::Selectable("(none)")) {
                    commit(scene::Value{std::string{}});
                    ImGui::CloseCurrentPopup();
                }

                std::size_t shown = 0;
                for (const std::string& candidate : candidates) {
                    if (!needle.empty() && !containsFold(candidate, needle))
                        continue;
                    ++shown;
                    if (ImGui::Selectable(candidate.c_str())) {
                        commit(scene::Value{std::string("asset://") + candidate});
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (candidates.empty()) {
                    ImGui::TextDisabled("No %s files under content/.", kindName.empty() ? "matching" : kindName.data());
                }
                else if (shown == 0) {
                    ImGui::TextDisabled("Nothing matches \"%s\".", search.data());
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        break;
    }
    case EditorKind::Vector3: {
        const core::Vec3 value = std::get<core::Vec3>(shared.value);
        float components[3]{value.x, value.y, value.z};
        if (ImGui::DragFloat3("##value", components, 0.01f, 0.0f, 0.0f, mixed ? "--" : "%.3f"))
            commit(scene::Value{core::Vec3{components[0], components[1], components[2]}});
        break;
    }
    case EditorKind::CFrame: {
        core::CFrameD value = std::get<core::CFrameD>(shared.value);
        f64 position[3]{value.position.x, value.position.y, value.position.z};
        if (ImGui::DragScalarN("##value", ImGuiDataType_Double, position, 3, 0.01f, nullptr, nullptr,
                               mixed ? "--" : "%.3f")) {
            value.position = core::DVec3{position[0], position[1], position[2]};
            commit(scene::Value{value});
        }
        // The basis is shown and never edited. A 3x3 rotation has no honest
        // widget, and round-tripping it through Euler angles would rewrite the
        // matrix on every frame the panel is open -- a property-changed fire
        // per frame for a value nobody touched.
        for (int axis = 0; axis < 3; ++axis) {
            if (mixed) {
                ImGui::TextUnformatted("-- -- --");
                continue;
            }
            ImGui::Text("%.3f %.3f %.3f", static_cast<f64>(value.rotation.m[axis][0]),
                        static_cast<f64>(value.rotation.m[axis][1]), static_cast<f64>(value.rotation.m[axis][2]));
        }
        break;
    }
    case EditorKind::Color: {
        const core::Color3 value = std::get<core::Color3>(shared.value);
        float components[3]{value.r, value.g, value.b};
        // Float and HDR because api-design.md 2.3 leaves the range open: a
        // picker that clamped to [0, 1] would silently rewrite a light's
        // intensity the first time anyone looked at it.
        //
        // A mixed colour shows the first member's swatch, which is the one
        // widget here that cannot say otherwise -- a picker with no colour in
        // it is not a picker. The row's `(mixed)` tag is what carries it.
        if (ImGui::ColorEdit3("##value", components, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
            commit(scene::Value{core::Color3{components[0], components[1], components[2]}});
        break;
    }
    case EditorKind::Vector2: {
        const core::Vec2 value = std::get<core::Vec2>(shared.value);
        float components[2]{value.x, value.y};
        if (ImGui::DragFloat2("##value", components, 0.5f, 0.0f, 0.0f, mixed ? "--" : "%.3f"))
            commit(scene::Value{core::Vec2{components[0], components[1]}});
        break;
    }
    case EditorKind::Rect: {
        const core::Rect value = std::get<core::Rect>(shared.value);
        float components[4]{value.min.x, value.min.y, value.max.x, value.max.y};
        if (ImGui::DragFloat4("##value", components, 1.0f, 0.0f, 0.0f, mixed ? "--" : "%.3f")) {
            commit(scene::Value{
                core::Rect{core::Vec2{components[0], components[1]}, core::Vec2{components[2], components[3]}}});
        }
        break;
    }
    case EditorKind::EnumCombo: {
        const scene::EnumValue value = std::get<scene::EnumValue>(shared.value);

        // The domain comes from the DESCRIPTOR, not from the value in the field.
        // Reading it off the value made the combo depend on the instance being
        // there and holding something -- an unset enum property offered no items
        // at all, and there was no way to ask what a property accepts without
        // creating one first. `enumDomainOf` answers from the class.
        //
        // The value's own enum is the fallback and not the source: a hand-built
        // registry (the fixtures) may declare a property without naming its
        // enum, and a field that then rendered nothing would be a regression
        // dressed as a refactor.
        scene::EnumId domain = enumDomainOf(world.enums(), descriptor);
        if (domain == scene::InvalidEnum)
            domain = value.enumId;

        const scene::EnumDescriptor* enumDescriptor = world.enums().find(domain);
        const std::string preview = mixed ? std::string("mixed") : formatValue(world, shared.value);
        if (enumDescriptor == nullptr) {
            ImGui::TextUnformatted(preview.c_str());
            break;
        }
        if (ImGui::BeginCombo("##value", preview.c_str())) {
            // Declaration order, which is `GetEnumItems`'s documented order and
            // therefore not something a panel gets to re-sort either.
            for (const scene::EnumItemDesc& item : enumDescriptor->items) {
                const std::string itemName(world.atoms().text(item.name));
                // Nothing is ticked while the members disagree: a tick would
                // name one of them as the selection's answer.
                const bool selected = !mixed && domain == value.enumId && item.value == value.value;
                if (ImGui::Selectable(itemName.c_str(), selected))
                    commit(scene::Value{scene::EnumValue{domain, item.value}});
            }
            ImGui::EndCombo();
        }
        break;
    }
    // **The two composites are SUMMARISED here and edited in their own rows**
    // (`drawCompositeRows`). This is the collapsed line -- `{1.000, -24}, {0.000,
    // 20}`, the same string `formatValue` has printed since M6 -- and the rows
    // underneath are where the numbers are. A box is for a value; the name of
    // the value belongs in the column that holds every other name in this panel.
    case EditorKind::UDim:
    case EditorKind::UDim2:
    case EditorKind::InstanceRef:
    case EditorKind::ReadOnlyText: {
        // The floor every `ValueType` falls back to, so that one with no editor
        // of its own is still inspectable rather than absent (M4 brief,
        // entering risk 6).
        const std::string text = mixed ? std::string("mixed") : formatValue(world, shared.value);
        ImGui::TextUnformatted(text.c_str());
        break;
    }
    }

    if (locked)
        ImGui::EndDisabled();

    ImGui::PopID();
}

// The gesture this panel currently owns, or zero. File-static for the same
// reason the row buffers above are: the panel is one function called once a
// frame, and its state between frames has nowhere else to live.
core::u64 g_propertyGesture = 0;

void drawProperties(scene::World& world, Inspector& inspector, ContentTree* tree = nullptr)
{
    // **The whole selection, not the primary.** A grid pointed at one instance
    // while three are highlighted is the editor disagreeing with itself, and it
    // is how somebody ends up moving one part and believing they moved three.
    const std::span<const core::InstanceId> targets = inspector.selectionSet();

    core::usize live = 0;
    for (const core::InstanceId id : targets)
        live += world.alive(id) ? 1u : 0u;

    if (live == 0) {
        ImGui::TextUnformatted("nothing selected");
        return;
    }

    // Said out loud, because a grid that looks identical for one instance and
    // for forty is one somebody edits forty instances with by accident.
    if (live > 1)
        ImGui::Text("%zu instances selected - every edit writes to all of them", live);

    // One loop over the descriptor tables. There is no switch on a class name
    // anywhere below this line, which is Decision 16's whole claim.
    collectCommonProperties(world, targets, g_properties);

    if (g_properties.empty()) {
        // Two classes with nothing in common is a legal selection and an empty
        // grid is the honest answer -- but an empty panel with no sentence in
        // it reads as a bug in the panel.
        ImGui::TextUnformatted("these classes share no properties");
        return;
    }

    if (ImGui::BeginTable("properties", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("property", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.55f);

        for (const scene::PropertyDesc* descriptor : g_properties) {
            ImGui::PushID(static_cast<int>(descriptor->name.id));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            const std::string_view propertyName = world.atoms().text(descriptor->name);
            const EditorKind kind = editorFor(*descriptor);

            // **A composite's name is a disclosure**, open by default: the rows
            // under it are where its numbers are edited, and starting closed
            // would put an extra click between a person and the one property
            // they opened the panel for. Collapsing it is theirs to do, and
            // ImGui remembers it per row.
            char nameLabel[128];
            (void)std::snprintf(nameLabel, sizeof(nameLabel), "%.*s", static_cast<int>(propertyName.size()),
                                propertyName.data());
            bool expanded = false;
            if (compositeKind(kind)) {
                expanded =
                    ImGui::TreeNodeEx(nameLabel, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                     ImGuiTreeNodeFlags_SpanAvailWidth);
            }
            else {
                ImGui::TextUnformatted(nameLabel);
            }

            // The IDL's own prose for this property, which now rides on the
            // descriptor rather than staying in a file nothing at runtime reads
            // (`class_registry.h` says why it is prose and not a catalog key).
            // Wrapped, because these are paragraphs and an unwrapped tooltip is
            // one line as wide as the sentence.
            if (descriptor->doc[0] != 0 && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
                ImGui::TextUnformatted(descriptor->doc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            if (const char* tag = propertyTag(*descriptor); tag != nullptr) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", tag);
                // The tooltip carries the part a three-character tag cannot: an
                // inert property is not broken and not read-only, it is waiting
                // for the milestone that renders it.
                if (descriptor->inert && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("stored and read back faithfully; nothing in this build acts on it yet");
                }
            }

            // Read once and handed to the editor. Asking twice would be two
            // sweeps of the selection per row per frame, and the tag and the
            // widget could then disagree.
            const SharedValue shared = sharedValue(world, targets, descriptor->name);
            if (shared.state == SharedState::Mixed) {
                ImGui::SameLine();
                ImGui::TextDisabled("(mixed)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("the selected instances hold different values; editing this sets all of them");
            }

            ImGui::TableSetColumnIndex(1);
            drawEditor(world, inspector, targets, *descriptor, shared, tree);

            // The rows a composite is actually edited in. Disabled with the
            // same rule the widget above uses, because they are the same
            // property: a read-only `UDim` must not offer four live drags.
            if (expanded && shared.state != SharedState::Unreadable) {
                const bool locked = !editable(*descriptor);
                if (locked)
                    ImGui::BeginDisabled();
                scene::Value edited;
                if (drawCompositeRows(kind, shared, edited)) {
                    for (const core::InstanceId target : targets) {
                        if (world.alive(target))
                            inspector.enqueue(target, descriptor->name, edited);
                    }
                }
                if (locked)
                    ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // **A drag in this panel is ONE edit**, and this is what tells the undo
    // stack so. ImGui keeps exactly one item active at a time, so "something in
    // this window is being held" is the whole of the question -- which is why
    // this is four lines here rather than a pair of calls repeated through
    // twelve widget branches, three of which draw more than one widget and
    // would each have got it subtly wrong.
    //
    // It matters more over a selection than it ever did over one instance: a
    // drag that writes to forty parts enqueues forty writes a frame, and
    // without a gesture every one of those frames is a world snapshot.
    //
    // The panel closes only the gesture it opened. The manipulators open their
    // own, and a properties panel that ended somebody else's drag because
    // nothing in it was focused would be worse than not tracking one at all.
    const bool holding = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsAnyItemActive();
    if (holding && g_propertyGesture == 0) {
        g_propertyGesture = inspector.beginGesture();
    }
    else if (!holding && g_propertyGesture != 0) {
        if (inspector.gesture() == g_propertyGesture)
            inspector.endGesture();
        g_propertyGesture = 0;
    }
}

// The one frame of latency Decision 15 buys determinism with, said out loud,
// plus what the last few writes actually did. A refusal that is not reported is
// a value that snaps back with no explanation.
void drawWriteLog(scene::World& world, const Inspector& inspector)
{
    if (inspector.pendingCount() > 0)
        ImGui::Text("%zu write(s) queued for the next frame start", inspector.pendingCount());

    const std::span<const WriteOutcome> outcomes = inspector.outcomes();
    if (outcomes.empty())
        return;

    if (!ImGui::CollapsingHeader("recent writes"))
        return;

    for (const WriteOutcome& outcome : outcomes) {
        const std::string_view propertyName = world.atoms().text(outcome.property);
        ImGui::Text("%.*s: %s", static_cast<int>(propertyName.size()), propertyName.data(),
                    setResultLabel(outcome.result));
    }
}

// --- The console and the memory table (D017) ---------------------------------
//
// `architecture.md` §app names five panes for the DebugShell -- explorer,
// properties, profiler with the memcat table, log/REPL, streaming map, physics
// wireframe -- and two of them had never been written. The audit that found that
// is what D017 is; this is the pane.

// The last few hundred log lines, and the sink that fills them.
//
// Bounded and dropping the oldest, because a shell that grew with the log would
// be a memory leak with a scrollbar. Process-global like the sink it installs:
// `core::setLogSink` takes one function and there is one console.
struct ConsoleLog
{
    static constexpr core::usize kMaxLines = 400;

    struct Line
    {
        core::LogLevel level = core::LogLevel::Info;
        std::string text;
    };

    std::mutex mutex;
    std::deque<Line> lines;
    bool installed = false;
    // The sink that was there first. Chained rather than replaced, so the
    // console pane and the log FILE both get every line -- a shell that ate the
    // log would be the last place anybody looked for it.
    core::LogSink previous;
};

ConsoleLog& console()
{
    static ConsoleLog instance;
    return instance;
}

void drawMemory(script::ScriptRuntime& runtime)
{
    const std::vector<script::ScriptRuntime::MemoryCategory> rows = runtime.memoryByCategory();

    core::usize total = 0;
    for (const auto& row : rows)
        total += row.bytes;
    ImGui::Text("script heap: %.1f KB across %d categories", static_cast<double>(total) / 1024.0,
                static_cast<int>(rows.size()));

    if (!ImGui::BeginTable("memcat", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        return;

    ImGui::TableSetupColumn("cat", ImGuiTableColumnFlags_WidthFixed, 32.0f);
    ImGui::TableSetupColumn("what");
    ImGui::TableSetupColumn("KB", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableHeadersRow();

    for (const auto& row : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", row.category);
        ImGui::TableNextColumn();
        // A category with no name is one the pool assigned and whose script has
        // since been replaced -- worth showing as a number rather than hiding,
        // because that is exactly the leak the table is for.
        ImGui::TextUnformatted(row.name.empty() ? "(recycled)" : std::string(row.name).c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", static_cast<double>(row.bytes) / 1024.0);
    }
    ImGui::EndTable();
}

// Whether `line` matches what is typed in the filter, case-insensitively.
//
// Case-insensitive because a person hunting a warning types what they remember
// and not what was printed, and a filter that misses `Chunk` for `chunk` is a
// filter that reads as broken.
[[nodiscard]] bool consoleMatches(std::string_view line, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > line.size())
        return false;
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    for (std::string_view::size_type at = 0; at + needle.size() <= line.size(); ++at) {
        std::string_view::size_type i = 0;
        while (i < needle.size() && lower(line[at + i]) == lower(needle[i]))
            ++i;
        if (i == needle.size())
            return true;
    }
    return false;
}

void drawConsole(script::ScriptRuntime* runtime)
{
    ConsoleLog& log = console();

    // **Clear and a filter, above the log they act on.** Both are things a
    // person reaches for when the console has become unreadable, which is
    // exactly when hunting for the control must not be part of the work.
    static std::array<char, 128> filter{};
    bool cleared = false;
    if (ImGui::Button("Clear"))
        cleared = true;
    ImGui::SameLine();
    const float resetWidth = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(resetWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##filter", "search", filter.data(), filter.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(filter[0] == 0);
    if (ImGui::Button("Reset"))
        filter.fill(0);
    ImGui::EndDisabled();

    const std::string_view needle{filter.data()};
    core::usize shown = 0;

    if (ImGui::BeginChild("log", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Borders)) {
        std::lock_guard<std::mutex> lock(log.mutex);
        // **Cleared under the same lock the sink writes under.** A clear that
        // raced a line from another thread would drop one that arrived after the
        // button and before the erase, which is the one kind of missing message
        // nobody would ever explain.
        if (cleared)
            log.lines.clear();
        for (const ConsoleLog::Line& line : log.lines) {
            if (!consoleMatches(line.text, needle))
                continue;
            ++shown;
            const ImVec4 colour = line.level == core::LogLevel::Error   ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                  : line.level == core::LogLevel::Warn  ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                                  : line.level == core::LogLevel::Debug ? ImVec4(0.6f, 0.65f, 0.75f, 1.0f)
                                                                        : ImVec4(0.85f, 0.88f, 0.92f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
        }
        // **A filter that hides everything says so.** An empty pane and a pane
        // filtered down to nothing look identical, and one of them means the
        // engine has said nothing while the other means you are not looking at
        // what it said.
        if (shown == 0 && !log.lines.empty())
            ImGui::TextDisabled("%zu line(s) hidden by the filter.", static_cast<std::size_t>(log.lines.size()));

        // Only while already at the bottom, so scrolling back to read something
        // is not undone by the next log line.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    static std::array<char, 512> input{};
    ImGui::SetNextItemWidth(-1.0f);
    const bool submitted = ImGui::InputText("##repl", input.data(), input.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (!submitted)
        return;

    const std::string_view source{input.data()};
    if (!source.empty() && runtime != nullptr) {
        // Echoed first, so the log reads as a session rather than as a list of
        // answers with no questions.
        core::logText(core::LogLevel::Info, std::string("> ").append(source));
        if (const std::optional<core::EngineError> error = runtime->evaluate(source); error.has_value())
            core::logText(core::LogLevel::Error, error->message);
    }
    input.fill(0);
    ImGui::SetKeyboardFocusHere(-1);
}

// Play, pause and step, above the image they act on.
//
// **There is no Stop, and its absence is deliberate** (D058). Stop means "put
// the world back the way it was before I pressed play", and this engine cannot
// remember an edited world yet -- nothing can serialize one. A Stop that
// silently rebuilt from the scripts would throw away whatever somebody had
// changed, which is worse than a button that is not there.
// The transport, in the order and the shape Unity and Unreal both use.
//
// **Play and stop are one button because they are opposites**; pause is a
// different question and gets its own. A single toggle cannot answer both --
// pressing play asks "run my game", pressing stop asks "give me my world back",
// and a button that means one of them while showing the other is the first
// thing a person notices.
void drawTransport(Editor& editor, EditorCommands& commands, const IconAtlas* icons)
{
    const RunState run = editor.runState();
    const bool inPlay = editor.inPlayMode();

    // **A toolbar button is its icon, and its label is the tooltip.** Six drawn
    // words in a row is a sentence somebody reads; six pictures is a control
    // panel they aim at. Falls back to the word when there is no atlas, because
    // a button with nothing on it is not a smaller button.
    const float glyph = ImGui::GetFrameHeight() - ImGui::GetStyle().FramePadding.y * 2.0f;
    const auto toolButton = [&](std::string_view id, const char* word, const char* tip) {
        return iconButton(icons, id, glyph, word, word, tip);
    };

    // **A stamp is open, so the transport is the STAMP's.** Play, pause and
    // step have nothing to mean on a stage -- nothing there ticks -- and what a
    // person wants in their place is exactly what a session offers: keep it,
    // keep it and leave, or leave without keeping it. Same corner of the screen
    // as the thing it replaces, because that is where a hand already goes.
    if (editor.stampSession().open()) {
        const Editor::StampSession& session = editor.stampSession();
        if (toolButton(icons::ActionSave, "save", "save this stamp and keep editing"))
            commands.saveStamp = true;

        ImGui::SameLine();
        if (toolButton(icons::ActionClose, "close", "save and go back to the scene")) {
            commands.closeStamp = true;
            commands.closeStampSaving = true;
        }

        // Only while there is something to discard: a control that would do
        // nothing is worse than no control, and one that throws work away
        // should not sit there on a session with nothing to throw.
        if (session.dirty) {
            ImGui::SameLine();
            if (toolButton(icons::ActionDelete, "discard",
                           "go back WITHOUT saving -- the edits since the last save are dropped")) {
                commands.closeStamp = true;
                commands.closeStampSaving = false;
            }
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        (void)drawIcon(icons, icons::ClassModel, glyph);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::PushStyleColor(ImGuiCol_Text, themeColor(palette().warning));
        ImGui::Text("%s%s", session.path.c_str(), session.dirty ? " *" : "");
        ImGui::PopStyleColor();
        return;
    }

    if (toolButton(inPlay ? icons::ActionStop : icons::ActionPlay, inPlay ? "stop" : "play",
                   inPlay ? "leave play mode and put the world back where you pressed play"
                          : "run the game, remembering the world first")) {
        commands.play = !inPlay;
    }

    // Only inside play mode, because pausing is a thing that happens to a
    // running game. Disabled rather than hidden: a control that appears and
    // disappears moves the ones beside it.
    ImGui::SameLine();
    ImGui::BeginDisabled(!inPlay);
    if (toolButton(run == RunState::Paused ? icons::ActionPlay : icons::ActionPause,
                   run == RunState::Paused ? "resume" : "pause", "hold the running world still")) {
        commands.pause = run != RunState::Paused;
    }
    ImGui::EndDisabled();

    // **Hidden while editing, and only usable while paused.** A step is one
    // tick of the simulation; an edited world is one whose simulation is
    // deliberately stopped, so the control has nothing to mean there. Inside
    // play mode it stays PRESENT and greys while running, because a control
    // that appears and disappears moves the ones beside it -- which is the same
    // rule pause is drawn by, applied one state further in.
    if (inPlay) {
        ImGui::SameLine();
        ImGui::BeginDisabled(run != RunState::Paused);
        if (toolButton(icons::ActionForward, "step", "advance exactly one simulation tick"))
            editor.requestStep();
        ImGui::EndDisabled();
    }

    // --- The manipulators -----------------------------------------------
    //
    // Three modes, the space they work in, and the grid. Grouped after the
    // transport and before the file actions, because that is the order somebody
    // reaches for them in: run it, then change it, then keep it.
    ImGui::SameLine();
    ImGui::TextDisabled("|");

    const auto modeButton = [&](GizmoMode mode, std::string_view id, const char* word, const char* tip) {
        ImGui::SameLine();
        const bool on = editor.gizmoMode() == mode;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (toolButton(id, word, tip))
            editor.setGizmoMode(mode);
        if (on)
            ImGui::PopStyleColor();
    };
    modeButton(GizmoMode::Translate, icons::ActionMove, "move", "move the selection  (W)");
    modeButton(GizmoMode::Rotate, icons::ActionRotate, "turn", "turn the selection  (E)");
    modeButton(GizmoMode::Scale, icons::ActionScale, "size", "resize the selection  (R)");

    // **Greyed while resizing, because a size has no world space to be in.**
    // Three numbers in the part's own frame is what a `Size` is, so the scale
    // handles are always the part's own -- and a toggle that stayed live while
    // changing nothing is a control that lies twice: once by looking usable,
    // and once by reading "world" over local handles.
    ImGui::SameLine();
    const bool sizing = editor.gizmoMode() == GizmoMode::Scale;
    ImGui::BeginDisabled(sizing);
    if (ImGui::Button(sizing || editor.gizmoLocal() ? "local" : "world"))
        editor.setGizmoLocal(!editor.gizmoLocal());
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(sizing                ? "a size is in the part's own axes, so resizing is always local"
                          : editor.gizmoLocal() ? "the selection's own axes -- click for the world's"
                                                : "the world's axes -- click for the selection's own");

    ImGui::SameLine();
    {
        const bool snapping = editor.snapping();
        if (snapping)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (toolButton(icons::ActionGrid, "snap", "snap to the grid  (hold Alt to suspend)"))
            editor.setSnap(!editor.snapping());
        if (snapping)
            ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");

    // A world to start in. Beside save rather than in the content browser,
    // because "give me somewhere to begin" is a thing you do to the WORLD and
    // the browser is about files.
    ImGui::SameLine();
    if (toolButton(icons::ActionNew, "new", "empty the scene and start over -- anything unsaved is gone"))
        commands.newScene = true;

    ImGui::SameLine();
    // **Save asks for a name when there is nothing to overwrite.** An editor
    // that invented one would put somebody's first hour of work in a file they
    // did not choose and cannot find, which is the whole reason every editor
    // has this dialog. The dialog itself lives at the shell's level, because
    // File > Save Scene As has to reach the same one.
    const bool untitled = editor.openScenePath().empty();
    if (toolButton(icons::ActionSave, untitled ? "save as..." : "save",
                   untitled ? "this scene has no name yet -- choose one" : editor.openScenePath().c_str())) {
        if (untitled)
            commands.wantSaveAs = true;
        else
            commands.save = true;
    }

    ImGui::SameLine();
    switch (run) {
    case RunState::Playing:
        ImGui::TextDisabled("playing");
        break;
    case RunState::Paused:
        ImGui::TextDisabled("paused in play mode -- stop to get your world back");
        break;
    case RunState::Editing:
        ImGui::TextDisabled("editing | right-drag to look, WASD/QE to fly, wheel for speed (%.0f m/s)",
                            static_cast<double>(editor.cameraSpeed()));
        break;
    }

    // Whatever the last save or stop had to say, on the line under the buttons
    // rather than in a modal: somebody pressing save twice a minute should not
    // have to dismiss anything.
    if (!editor.status().message.empty()) {
        const ImVec4 colour = editor.status().failed ? themeColor(palette().danger) : themeColor(palette().success);
        ImGui::TextColored(colour, "%s", editor.status().message.c_str());
    }
}

// What the mouse and keyboard are asking the viewport for.
//
// **It reports and does not act.** The camera is driven by the frame loop,
// because turning it puts the pointer into relative mode and in relative mode
// ImGui has no absolute position to difference -- so `io.MouseDelta` is zero
// exactly when the camera needs it. The motion comes from the platform's own
// events; what a UI callback can still answer is whether somebody is asking.
//
// RIGHT button, not left: left is select, and an editor where looking around
// changes what you have selected is unusable. Held rather than toggled, because
// a mode you can forget you are in is how a minute goes missing.
void reportLookInput(Editor& editor, bool overViewport)
{
    const ImGuiIO& io = ImGui::GetIO();

    // The wheel changes SPEED rather than dollying. A dolly duplicates what W
    // and S already do; a speed control is the thing a four-kilometre world and
    // a four-metre room need different values of.
    if (overViewport && io.MouseWheel != 0.0f)
        editor.setCameraSpeed(editor.cameraSpeed() * (io.MouseWheel > 0.0f ? 1.25f : 0.8f));

    // **Latched on the press, and only there.** The first version asked "over
    // the viewport OR dragging", and `IsMouseDragging` becomes true a pixel
    // after a press ANYWHERE -- so a right-click in the explorer turned into a
    // camera turn: the pointer locked, motion stopped reaching ImGui, and the
    // context menu that click was asking for never appeared.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        g_lookLatched = overViewport;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        g_lookLatched = false;

    Editor::LookInput look;
    look.active = g_lookLatched;

    if (look.active) {
        const auto axis = [](ImGuiKey positive, ImGuiKey negative) -> f32 {
            return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
        };
        const f32 sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f;
        look.move =
            core::Vec3{axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_E, ImGuiKey_Q), axis(ImGuiKey_W, ImGuiKey_S)} *
            sprint;
    }

    editor.setLookInput(look);
}

// The 3D view.
//
// The panel IS the image: no padding, because a margin of window background
// around a rendered world reads as a bug rather than as a frame. Its rectangle
// is handed to the editor every frame because that rectangle is the only thing
// that maps a mouse position onto a ray.
// The image and everything that reads a pointer over it, without the window
// around it.
//
// Split out so that F3 can show the WORLD with none of the furniture -- see
// `drawViewportFullscreen`. It sets the editor's viewport rect from whatever
// window it is called inside, which is what keeps the aspect ratio, the picking
// ray and the image somebody is looking at agreeing with each other whichever
// of the two is drawing.
void drawViewportBody(Editor& editor, rhi::TextureHandle texture, EditorCommands& commands)
{
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    editor.setViewport(ViewportRect{origin.x, origin.y, size.x, size.y});

    SDL_GPUTexture* native = texture.valid() ? rhi::nativeTexture(*g_device, texture) : nullptr;
    if (native != nullptr && size.x >= 1.0f && size.y >= 1.0f) {
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(native)), size);

        // **Dropped into the WORLD** (ADR 0052): dragging something out of
        // the project's tree and onto the viewport is the shortest way to
        // say "one of those, here". It lands under `Workspace` rather than
        // where the pointer is, because a drop point is a pixel and a
        // placement is a position -- the two only meet through a pick, and
        // a prefab that landed inside whatever happened to be behind the
        // cursor would be a surprise every time it worked.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* dropped = ImGui::AcceptDragDropPayload(kContentDragPayload); dropped != nullptr) {
                commands.placeStamp = static_cast<const ContentDrag*>(dropped->Data)->path;
                commands.placeStampLinked = true;
                commands.placeStampParent = {};
            }
            ImGui::EndDragDropTarget();
        }

        // Hovering the IMAGE, not the window: a click on the tab, the
        // border or the space beside a letterboxed image is not a click on
        // the world, and treating it as one deselects whatever the person
        // was working on.
        const bool overImage = ImGui::IsItemHovered();
        const ImVec2 mouse = ImGui::GetMousePos();
        const core::Vec2 inViewport{mouse.x - origin.x, mouse.y - origin.y};

        // **The pointer, every frame, not just on a click.** A manipulator
        // needs where it is and whether the button is held; a click alone
        // cannot say either. `pressed` is gated on the image because a drag
        // that began in another panel is not this one's, and `down` is NOT,
        // because a drag that leaves the viewport is still a drag and one
        // that ends outside it still ends.
        editor.setPointer(inViewport, overImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                          ImGui::IsMouseDown(ImGuiMouseButton_Left));

        if (overImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            editor.requestPick(inViewport, ImGui::GetIO().KeyCtrl);

        reportLookInput(editor, overImage);
    }
}

void drawViewport(Editor& editor, rhi::TextureHandle texture, EditorCommands& commands, bool& open,
                  const IconAtlas* icons)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Viewport", &open);
    ImGui::PopStyleVar();

    if (visible) {
        drawTransport(editor, commands, icons);
        drawViewportBody(editor, texture, commands);
    }
    ImGui::End();
}

// **What F3 shows in the editor**, and the reason it has to show anything at
// all: in the editor the world is rendered into a TEXTURE and the screen itself
// is only cleared, so hiding the panels hides the world with them -- a black
// window, reported as one. The overlay's own contract calls F3 "the cheapest
// way to look at the world without the furniture", and this is the half that
// makes that true.
//
// A window of its own rather than the docked one undocked: the layout somebody
// arranged is theirs, and a keystroke that rearranged it would cost more than
// it showed. It sets the viewport rect to the whole screen while it is up, so
// the view is the view and a click still lands where it looks.
void drawViewportFullscreen(Editor& editor, rhi::TextureHandle texture, EditorCommands& commands)
{
    const ImGuiViewport* screen = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(screen->Pos);
    ImGui::SetNextWindowSize(screen->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
                                       ImGuiWindowFlags_NoBackground;
    const bool visible = ImGui::Begin("fullscreen-viewport", nullptr, flags);
    ImGui::PopStyleVar();
    if (visible) {
        drawViewportBody(editor, texture, commands);
    }
    ImGui::End();
}

// The arrangement somebody gets the first time they open the editor.
//
// Without this every panel is placed at ImGui's default position, which is the
// same position, so the first launch is five windows in a pile with the
// viewport at the bottom of it -- which is what the first run of this shell
// actually looked like. A dockspace does not arrange anything by itself; it
// only makes arranging possible.
//
// Built once, and only when there is no saved layout: `DockBuilderRemoveNode`
// would throw away the arrangement somebody chose.
void buildDefaultLayout(ImGuiID dockspace)
{
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);

    // The centre is the world and everything else is furniture around it, which
    // is the one thing every editor of this shape agrees on.
    ImGuiID centre = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.22f, nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.28f, nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.26f, nullptr, &centre);

    ImGui::DockBuilderDockWindow("Viewport", centre);
    ImGui::DockBuilderDockWindow("Explorer", left);
    // Properties first for the reason content is: a tab node opens on whichever
    // window was docked LAST, and "which one greets somebody" is a decision
    // rather than a consequence of call order -- so it is set explicitly below
    // and the order here is only what makes the tabs read left to right.
    ImGui::DockBuilderDockWindow("Properties", right);
    ImGui::DockBuilderDockWindow("Stats", right);
    // Content first, so it is the tab that opens. The two share a node on
    // purpose -- they are both "the thing under the viewport" and neither
    // deserves permanent floor space -- but which one greets somebody is a
    // decision rather than a consequence of call order, so it is also set
    // explicitly below.
    ImGui::DockBuilderDockWindow("Content", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);

    ImGui::DockBuilderFinish(dockspace);
}

// A short label for a kind, so a row says what it is without an icon set.
// The icon id for a content kind, straight off the enum -- the same mechanical
// mapping `class.` uses, which is what `icons/README.md` means by "content.
// maps straight off the `ContentKind` enum".
[[nodiscard]] std::string_view contentKindIcon(ContentKind kind) noexcept
{
    switch (kind) {
    case ContentKind::Folder:
        return icons::ContentFolder;
    case ContentKind::Scene:
        return icons::ContentScene;
    case ContentKind::Stamp:
        // **A `Model` wearing the stamp badge**, which is not a stand-in for a
        // drawing the set owes -- it is the same sentence the Explorer makes,
        // read the other way round. There, a badged icon says "this instance
        // comes from a file"; here it says "this file is what one comes from",
        // and one mark for one idea beats two drawings for it. The base is a
        // `Model` because a stamp is a group of instances handled as one thing.
        // The badge is drawn by the browser, over this.
        return icons::ClassModel;
    case ContentKind::Mesh:
        return icons::ContentMesh;
    case ContentKind::Texture:
        return icons::ContentTexture;
    case ContentKind::Audio:
    case ContentKind::Font:
        // No drawing of their own yet, and the generic file is the honest
        // fallback rather than borrowing a mesh's or a texture's.
        return icons::ContentOther;
    case ContentKind::Chunk:
        return icons::ContentChunk;
    case ContentKind::Other:
        break;
    }
    return icons::ContentOther;
}

// The icon a content row wears.
//
// **A stamp wears the icon of the instance it is a file OF** -- a character's
// stamp draws as a character and a lamp post's as a part -- because that is the
// thing a person recognises in a folder of forty, and because a file of a
// character IS a character as far as anybody browsing is concerned. The kind's
// own icon is the fallback for a stamp whose root this build could not read or
// has no drawing for; the badge on top is what says "a file one comes from"
// rather than "the instance itself".
bool drawContentIcon(const IconAtlas* icons, const ContentEntry& entry, float size, std::optional<core::Color3> tint)
{
    if (entry.kind == ContentKind::Stamp && !entry.rootClass.empty() &&
        drawIcon(icons, "class." + entry.rootClass, size, tint)) {
        return true;
    }
    return drawIcon(icons, contentKindIcon(entry.kind), size, tint);
}

[[nodiscard]] const char* contentKindLabel(ContentKind kind) noexcept
{
    switch (kind) {
    case ContentKind::Folder:
        return "dir";
    case ContentKind::Scene:
        return "scene";
    case ContentKind::Stamp:
        return "stamp";
    case ContentKind::Mesh:
        return "mesh";
    case ContentKind::Texture:
        return "tex";
    case ContentKind::Audio:
        return "audio";
    case ContentKind::Font:
        return "font";
    case ContentKind::Chunk:
        return "chunk";
    case ContentKind::Other:
        break;
    }
    return "";
}

// The project's assets, and the panel from which a scene is opened.
//
// **Virtualised**, and that is a measurement rather than a flourish: the quality
// bar for this was given as Unity and Unreal, and a Content Browser is judged on
// a tree of thousands. `ImGuiListClipper` draws only the rows a person can see,
// so a folder of ten thousand meshes costs the same as a folder of ten.
// `drawExplorer` beside this one is virtualised twice over: the clipper draws
// what is on screen, and the walk that decides what could be on screen does not
// enter a closed subtree (ADR 0054).
// One entry of the browser, in whichever layout is on.
//
// **The three layouts share every decision except where things are put**, which
// is why this is one function with a switch inside rather than three: the double
// click that opens a scene, the menu that colours a folder, the green that marks
// the scene already open and the colour a person chose all belong to an ENTRY
// and not to a way of drawing one. Three copies of that would drift the first
// time any of it changed, which is exactly what happened to the Explorer's row
// before it was rebuilt around one height.
struct ContentLayout
{
    // The box one entry occupies. For a list that is the full width by a row's
    // height; for the other two it is a square somebody can aim at.
    ImVec2 cell;
    float icon = 0.0f;
    // Whether the name goes under the icon rather than beside it.
    bool nameBelow = false;
    // How many fit across. One for a list.
    int columns = 1;
};

[[nodiscard]] ContentLayout contentLayoutFor(EditorPanels::ContentView view, float available)
{
    ContentLayout layout;
    switch (view) {
    case EditorPanels::ContentView::List:
        layout.cell = ImVec2(0.0f, ImGui::GetFrameHeight());
        layout.icon = ImGui::GetTextLineHeight();
        layout.nameBelow = false;
        layout.columns = 1;
        return layout;
    case EditorPanels::ContentView::Tiles:
        layout.icon = ImGui::GetFrameHeight() * 1.6f;
        break;
    case EditorPanels::ContentView::Icons:
        layout.icon = ImGui::GetFrameHeight() * 3.0f;
        break;
    }

    const float padding = ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
    const float side = layout.icon + padding;
    layout.cell = ImVec2(side, layout.icon + ImGui::GetTextLineHeight() + padding);
    layout.nameBelow = true;
    // At least one, whatever the panel has been dragged down to: a column count
    // of zero is a division by it two lines later.
    layout.columns = std::max(1, static_cast<int>(available / (side + ImGui::GetStyle().ItemSpacing.x)));
    return layout;
}

// A name that does not fit, cut where it stops fitting and marked. Only the
// grids need it -- a list row has the whole panel width and simply runs on.
[[nodiscard]] std::string elideToWidth(const std::string& text, float width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;

    const float ellipsis = ImGui::CalcTextSize("...").x;
    std::string cut;
    cut.reserve(text.size());
    for (const char c : text) {
        cut.push_back(c);
        if (ImGui::CalcTextSize(cut.c_str()).x + ellipsis > width) {
            cut.pop_back();
            break;
        }
    }
    cut += "...";
    return cut;
}

// Opening another scene loses whatever is unsaved, so it asks the same question
// closing does -- through the same `dialogs.pending`, which is what keeps one
// answer for four doors (ADR 0055's launcher added the third and fourth).
void openSceneOrAsk(Editor& editor, EditorCommands& commands, EditorDialogs& dialogs, std::string_view path)
{
    if (editor.hasUnsavedWork()) {
        dialogs.pending = EditorDialogs::Pending::OpenScene;
        dialogs.pendingScene = std::string(path);
        return;
    }
    commands.openScene = std::string(path);
}

void drawContent(Editor& editor, EditorCommands& commands, EditorPanels& panels, EditorDialogs& dialogs,
                 const IconAtlas* icons)
{
    if (!ImGui::Begin("Content", &panels.content)) {
        ImGui::End();
        return;
    }

    ContentTree& tree = editor.content();
    const float toolbarIcon = ImGui::GetTextLineHeight();

    // **One line, two heights, and the taller one decides.** Everything on this
    // row is drawn at text height -- the icons, the separators, the path's own
    // words -- and the search box at the end of it is a FRAMED widget, taller by
    // twice the theme's padding. Left alone the short things sit at the top of
    // the line and the box hangs below them, which is what a person sees as a
    // toolbar that is not lined up.
    //
    // `AlignTextToFramePadding` fixes the TEXT and only the text: it moves the
    // line's baseline, and an `ImageButton` is not placed against a baseline. So
    // the icons are nudged by hand, and `onRow` is that nudge -- called before
    // each of them rather than once, because each is its own item.
    ImGui::AlignTextToFramePadding();
    const auto onRow = [](float itemHeight) {
        const float slack = ImGui::GetFrameHeight() - itemHeight;
        if (slack > 0.0f)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slack * 0.5f);
    };

    ImGui::BeginDisabled(tree.atRoot());
    if (iconButton(icons, icons::ActionUp, toolbarIcon, "up", "up", "up one folder", true, true))
        (void)tree.leave();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (iconButton(icons, icons::ActionRefresh, toolbarIcon, "refresh", "refresh", "re-read this folder", true, true))
        (void)tree.refresh();

    ImGui::SameLine();
    // The shell's dialog, so the toolbar button and the folder's own
    // right-click menu reach the same one. A FOLDER rather than a generic
    // "new", because what it makes is a folder and the picture can say so.
    if (iconButton(icons, icons::ContentFolder, toolbarIcon, "new-folder", "new folder", "new folder", true, true))
        dialogs.newFolder = true;

    // **Bringing a file in from the machine**, which is the other half of a
    // content browser: a project's assets come from somewhere, and until now the
    // only way in was a file manager beside the editor. Dropping files on the
    // window does the same thing and lands in the same folder.
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::BeginDisabled(!platform::canPickFolder());
    if (iconButton(icons, icons::ActionAdd, toolbarIcon, "import", "import", "import files into this folder", true,
                   true))
        commands.importAssets = true;
    ImGui::EndDisabled();

    // --- Where you are, as a row of steps you can click back through ---------
    //
    // **A path is a chain and the chain is the navigation.** Printing it as
    // text says where you are and offers nothing; every browser worth using
    // makes each step a way back to it, and the only thing that costs is
    // splitting a string somebody is already reading.
    ImGui::SameLine();
    ImGui::TextDisabled("|");

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    // **The icon AND the word, on every step including this one.** An icon
    // button's word is only its fallback for a missing atlas, so the first
    // step wore a picture and no name -- which reads as a button nobody named
    // rather than as the root of the chain.
    onRow(toolbarIcon);
    (void)drawIcon(icons, icons::ContentFolder, toolbarIcon);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (tree.atRoot()) {
        // Where you already are is a label, not a control: the step that goes
        // nowhere is drawn the same way at the end of the chain.
        ImGui::TextUnformatted("content");
    }
    else if (crumbButton("content")) {
        while (!tree.atRoot())
            (void)tree.leave();
    }

    {
        const std::string& relative = tree.currentFolder();
        // Counted first, because a step's job is "go up (depth - me - 1)
        // times" and it has to know how deep the chain is to say that.
        int depth = relative.empty() ? 0 : 1;
        for (const char c : relative)
            depth += c == '/' ? 1 : 0;

        std::size_t begin = 0;
        for (int step = 0; step < depth; ++step) {
            const std::size_t slash = relative.find('/', begin);
            const std::string segment =
                relative.substr(begin, slash == std::string::npos ? std::string::npos : slash - begin);
            // The path AS FAR AS THIS STEP, which is what a folder's colour is
            // keyed by -- a step is a folder, and the same folder wears the
            // same colour wherever it is drawn.
            const std::string here = slash == std::string::npos ? relative : relative.substr(0, slash);
            begin = slash == std::string::npos ? relative.size() : slash + 1;

            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

            ImGui::PushID(step);
            const bool last = step == depth - 1;
            // **A folder icon per step, not one at the front.** Every step in
            // the chain IS a folder, and a row that pictures only the first of
            // them says the rest are something else -- coloured, because a
            // folder somebody coloured is the same folder here.
            onRow(toolbarIcon);
            (void)drawIcon(icons, icons::ContentFolder, toolbarIcon, editor.contentColor(here));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            // The one you are IN is not a button: it goes nowhere, and a
            // control that does nothing is worse than a label.
            if (last) {
                ImGui::TextUnformatted(segment.c_str());
            }
            else if (crumbButton(segment.c_str())) {
                for (int up = 0; up < depth - step - 1; ++up)
                    (void)tree.leave();
            }
            ImGui::PopID();
        }
    }

    // **What you are looking for, in the folder you are in.**
    //
    // Per folder rather than across the tree, because that is what the panel
    // shows: a browser that answered with matches from somewhere else would be
    // a search result wearing a folder's chrome. Cleared when you leave the
    // folder, for the same reason -- a filter that survived the move would hide
    // most of wherever you arrived, and nothing on screen would say why.
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
    static std::array<char, 96> contentFilter{};
    static std::string contentFilterFolder;
    if (contentFilterFolder != tree.currentFolder()) {
        contentFilterFolder = tree.currentFolder();
        contentFilter.fill(0);
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    ImGui::InputTextWithHint("##content-filter", "search", contentFilter.data(), contentFilter.size());
    const std::string_view contentNeedle{contentFilter.data()};

    // **The view, as one button that opens the three.**
    //
    // Three buttons in a row was what this was and it read as a control panel
    // for a choice somebody makes twice a day. One button wearing the current
    // view's own icon says what is on without being asked, and the list behind
    // it says what else there is -- which is what Unreal's browser does with
    // the same question.
    {
        const char* names[] = {"List", "Tiles", "Icons"};
        const std::string_view viewIcon =
            panels.contentView == EditorPanels::ContentView::List ? icons::ActionList : icons::ActionGrid;

        const float room = ImGui::GetContentRegionAvail().x;
        const float wanted = toolbarIcon + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (room > wanted) {
            ImGui::SameLine(ImGui::GetCursorPosX() + room - wanted);
            if (iconButton(icons, viewIcon, toolbarIcon, "view", names[static_cast<int>(panels.contentView)],
                           "how entries are laid out", true, true)) {
                ImGui::OpenPopup("view-menu");
            }
        }
        if (ImGui::BeginPopup("view-menu")) {
            for (int index = 0; index < 3; ++index) {
                const auto view = static_cast<EditorPanels::ContentView>(index);
                if (ImGui::MenuItem(names[index], nullptr, panels.contentView == view)) {
                    panels.contentView = view;
                    // Written through to the editor, which is the one that has a
                    // file: this panel's copy dies with the run.
                    editor.setContentView(view);
                }
            }
            ImGui::EndPopup();
        }
    }

    ImGui::Separator();

    if (ImGui::BeginChild("entries")) {
        // What the filter left. Pointers into the tree's own vector, which
        // outlives this frame -- and a list rather than a test inside the loop,
        // because the grid indexes by position and a skipped entry would leave
        // a hole in it.
        static std::vector<const ContentEntry*> entryList;
        entryList.clear();
        for (const ContentEntry& candidate : tree.entries()) {
            if (contentNeedle.empty() || containsFold(candidate.name, contentNeedle))
                entryList.push_back(&candidate);
        }
        const std::vector<const ContentEntry*>& entries = entryList;
        const ContentLayout layout = contentLayoutFor(panels.contentView, ImGui::GetContentRegionAvail().x);
        const float entryHeight = layout.cell.y;

        // No vertical item spacing while the rows are drawn: a `Selectable`
        // pads its highlight with half of it on each side, which at an exact
        // pitch has nowhere to go but into the neighbours. Same rule as the
        // explorer's, same reason.
        const ImVec2 entrySpacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(entrySpacing.x, layout.nameBelow ? entrySpacing.y : 0.0f));

        const int columns = layout.columns;
        const int rows = (static_cast<int>(entries.size()) + columns - 1) / columns;
        const float pitch = entryHeight + (layout.nameBelow ? entrySpacing.y : 0.0f);
        const float strideX = layout.cell.x + entrySpacing.x;

        // **Every cell is placed outright, and `SameLine` is not used at all.**
        //
        // It was, and it produced a staircase: `SameLine` returns the cursor to
        // the previous ITEM's line, and each cell ends by putting the cursor a
        // row lower, so every cell after the first started one row down and one
        // column right of where it belonged. The Explorer learned the same
        // lesson one panel over -- a layout at an exact pitch has to compute the
        // position rather than accumulate it.
        const ImVec2 gridOrigin = ImGui::GetCursorPos();

        // **The whole grid's extent, claimed before a single cell is drawn.**
        // `SetCursorPos` moves the cursor without submitting anything, so
        // without this the child never learns how tall its contents are: it
        // cannot scroll, and ImGui says so once per row per frame. A `Dummy`
        // has no id, so it takes neither a click nor a hover from the cells
        // drawn over it.
        ImGui::Dummy(ImVec2(layout.cell.x > 0.0f ? strideX * static_cast<float>(columns) : 0.0f,
                            pitch * static_cast<float>(rows)));
        ImGui::SetCursorPos(gridOrigin);

        // **A filter that hides everything says so.** An empty folder and a
        // folder filtered down to nothing look identical, and one of them means
        // there is nothing here while the other means you are not looking at it.
        if (entries.empty() && !tree.entries().empty()) {
            ImGui::SetCursorPos(gridOrigin);
            ImGui::TextDisabled("Nothing here matches \"%s\".", contentFilter.data());
        }

        ImGuiListClipper clipper;
        clipper.Begin(rows, pitch);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const int index = row * columns + column;
                    if (index >= static_cast<int>(entries.size()))
                        break;
                    const ContentEntry& entry = *entries[static_cast<std::size_t>(index)];
                    ImGui::PushID(index);

                    const ImVec2 entryOrigin(gridOrigin.x + static_cast<float>(column) * strideX,
                                             gridOrigin.y + static_cast<float>(row) * pitch);
                    ImGui::SetCursorPos(entryOrigin);
                    const float entryIcon = layout.icon;

                    const bool isOpenScene = entry.kind == ContentKind::Scene && entry.path == editor.openScenePath();
                    if (isOpenScene)
                        ImGui::PushStyleColor(ImGuiCol_Text, themeColor(palette().accent));

                    // Double-click, because a single click is how somebody
                    // browses and opening a scene throws away what is in the
                    // world.
                    if (ImGui::Selectable("##entry", isOpenScene,
                                          ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
                                          layout.cell) &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (entry.kind == ContentKind::Folder)
                            (void)tree.enter(entry.name);
                        else if (entry.kind == ContentKind::Scene)
                            openSceneOrAsk(editor, commands, dialogs, entry.path);
                        // **Opening a stamp EDITS it**, the way opening a
                        // scene edits a scene -- which is the pairing that
                        // makes the browser one idea rather than two. Placing
                        // one in the world is the right-click, because it is
                        // the thing you do to a world rather than to a file.
                        else if (entry.kind == ContentKind::Stamp)
                            commands.openStamp = entry.path;
                    }

                    // **A prefab is dragged out of here into the world**, which
                    // is what a Project window is for: drop it on a row of the
                    // Explorer to place it there, or on the viewport to place
                    // it under `Workspace`.
                    if (entry.kind == ContentKind::Stamp &&
                        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                        ContentDrag payload;
                        (void)std::snprintf(payload.path, sizeof(payload.path), "%s", entry.path.c_str());
                        ImGui::SetDragDropPayload(kContentDragPayload, &payload, sizeof(payload));
                        ImGui::TextUnformatted(ContentTree::displayNameOf(entry).c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginPopupContextItem("entry-menu")) {
                        // The rows are drawn with no vertical spacing; a menu is
                        // not a row.
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, entrySpacing);
                        if (entry.kind == ContentKind::Scene && ImGui::MenuItem("Open"))
                            openSceneOrAsk(editor, commands, dialogs, entry.path);
                        if (entry.kind == ContentKind::Stamp) {
                            if (ImGui::MenuItem("Open"))
                                commands.openStamp = entry.path;
                            // **Two ways to place one, and both are real.** A
                            // linked instance follows the file; a copy is its
                            // own from the first frame. A lamp post you will
                            // place forty of wants the first; a starting point
                            // you are about to rebuild wants the second.
                            if (ImGui::MenuItem("Place (linked)")) {
                                commands.placeStamp = entry.path;
                                commands.placeStampLinked = true;
                            }
                            if (ImGui::MenuItem("Place a copy")) {
                                commands.placeStamp = entry.path;
                                commands.placeStampLinked = false;
                            }
                        }
                        // **A directory cannot carry a colour**, so this one is
                        // kept in `.luaug/editor.json` keyed by path, while a
                        // folder in the WORLD carries its own as an attribute.
                        // Two stores, because the two things are different --
                        // see `Editor::setFolderColor`.
                        if (entry.kind == ContentKind::Folder && ImGui::BeginMenu("Colour")) {
                            std::optional<core::Color3> chosen = editor.contentColor(entry.path);
                            if (colorMenu(chosen)) {
                                commands.colorAsked = true;
                                commands.colorTarget = {};
                                commands.colorContentPath = entry.path;
                                commands.color = chosen;
                            }
                            ImGui::EndMenu();
                        }
                        if (ImGui::MenuItem("Rename...")) {
                            dialogs.renameTarget = {};
                            dialogs.renameContentPath = entry.path;
                            dialogs.renameSeed = ContentTree::stemOf(entry);
                            dialogs.renameContent = true;
                        }
                        ImGui::Separator();
                        // Here as well as on the folder's own menu: a person who
                        // right-clicks lands on whatever was under the pointer,
                        // and which of the two menus they opened is not a
                        // distinction they made on purpose.
                        if (ImGui::MenuItem("Import...", nullptr, false, platform::canPickFolder()))
                            commands.importAssets = true;
                        if (ImGui::MenuItem("Delete"))
                            dialogs.deleteContentPath = entry.path;
                        ImGui::PopStyleVar();
                        ImGui::EndPopup();
                    }

                    const std::optional<core::Color3> tint =
                        entry.kind == ContentKind::Folder ? editor.contentColor(entry.path) : std::nullopt;

                    // Placed on the cell rather than stacked after it, for the
                    // reason the explorer's are: the icon and the text are two
                    // heights and the cell has one shape.
                    // **`lantern-post.stamp`, not `lantern-post.stamp.json`.**
                    // `.json` is how the file is stored and this panel is the
                    // one place that has to say what it IS.
                    const std::string shown = ContentTree::displayNameOf(entry);

                    if (layout.nameBelow) {
                        const float centreX = entryOrigin.x + (layout.cell.x - entryIcon) * 0.5f;
                        ImGui::SetCursorPos(ImVec2(centreX, entryOrigin.y + ImGui::GetStyle().ItemInnerSpacing.y));
                        const ImVec2 iconOrigin = ImGui::GetCursorScreenPos();
                        if (drawContentIcon(icons, entry, entryIcon, tint) && entry.kind == ContentKind::Stamp)
                            drawIconBadge(icons, iconOrigin, entryIcon);

                        const std::string label = elideToWidth(shown, layout.cell.x);
                        const float textX =
                            entryOrigin.x + (layout.cell.x - ImGui::CalcTextSize(label.c_str()).x) * 0.5f;
                        ImGui::SetCursorPos(
                            ImVec2(textX, entryOrigin.y + entryIcon + ImGui::GetStyle().ItemInnerSpacing.y));
                        ImGui::TextUnformatted(label.c_str());
                        // The kind is what the icon already says, and a grid
                        // cell has no room to say it twice.
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", entry.name.c_str());
                    }
                    else {
                        float entryX = entryOrigin.x;
                        ImGui::SetCursorPos(ImVec2(entryX, entryOrigin.y + (entryHeight - entryIcon) * 0.5f));
                        const ImVec2 iconOrigin = ImGui::GetCursorScreenPos();
                        if (drawContentIcon(icons, entry, entryIcon, tint)) {
                            // **The same badge the Explorer puts on a stamped
                            // instance**, on the file it stamps from. One mark
                            // for one idea: a person who has learned it in the
                            // hierarchy does not learn it again here, and the
                            // set can owe a `content.Stamp` drawing for as long
                            // as it likes without this row being mute.
                            if (entry.kind == ContentKind::Stamp)
                                drawIconBadge(icons, iconOrigin, entryIcon);
                            entryX += entryIcon + ImGui::GetStyle().ItemInnerSpacing.x;
                        }

                        const float textY = entryOrigin.y + (entryHeight - ImGui::GetTextLineHeight()) * 0.5f;
                        ImGui::SetCursorPos(ImVec2(entryX, textY));
                        ImGui::TextUnformatted(shown.c_str());
                        entryX += ImGui::CalcTextSize(shown.c_str()).x + 12.0f;

                        if (const char* label = contentKindLabel(entry.kind); label[0] != '\0') {
                            ImGui::SetCursorPos(ImVec2(entryX, textY));
                            ImGui::TextDisabled("%s", label);
                        }
                    }

                    // **The colour is popped after the NAME is drawn, not after
                    // the selectable.** It is the text's colour, and the text is
                    // placed on the cell rather than carried by the selectable's
                    // label -- so popping earlier would have coloured nothing.
                    if (isOpenScene)
                        ImGui::PopStyleColor();

                    ImGui::PopID();
                }
            }
        }
        ImGui::PopStyleVar();

        // **An instance dragged in from the Explorer becomes a PREFAB here**,
        // in the folder somebody dropped it in. That is what dragging from the
        // hierarchy into the Project window means in every editor that has
        // both, and it is the shortest sentence in this whole model: a prefab
        // is a thing in the world, saved.
        //
        // On the whole panel rather than on a row, because what it means is
        // "into this FOLDER" -- there is no row it could sensibly land on.
        if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("content-drop"))) {
            if (const ImGuiPayload* dropped = ImGui::AcceptDragDropPayload(kInstanceDragPayload); dropped != nullptr) {
                const InstanceDrag* incoming = static_cast<const InstanceDrag*>(dropped->Data);
                commands.stampSubject = incoming->id;
                // **The folder, not the name.** The name comes from the
                // instance, which this panel does not know -- it has an id --
                // so the drain composes the two. A dialog would be the other
                // answer and it is the wrong one: a box that opens on a DROP
                // interrupts the gesture that opened it, and renaming a file
                // afterwards is one click.
                commands.stampFolder = tree.currentFolder().empty() ? std::string(kStampFolder) : tree.currentFolder();
            }
            ImGui::EndDragDropTarget();
        }

        // The space below the rows. Right-clicking nothing is how somebody asks
        // about the FOLDER rather than about a thing in it.
        if (ImGui::BeginPopupContextWindow("folder-menu",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            // **Where a person looks for it.** A toolbar button is where the
            // thing lives; a right-click is where somebody goes when they have
            // just decided they want it, and an item that is only on the
            // toolbar is one people conclude does not exist.
            if (ImGui::MenuItem("Import...", nullptr, false, platform::canPickFolder()))
                commands.importAssets = true;
            ImGui::Separator();
            if (ImGui::MenuItem("New Folder..."))
                dialogs.newFolder = true;
            if (ImGui::MenuItem("Refresh"))
                (void)tree.refresh();
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// The application menu, which every engine has and which this one did not.
//
// **A menu bar is not decoration: it is where a person looks for a thing they
// cannot see.** A panel closed by accident, a scene saved under a new name, the
// preferences -- none of those have anywhere else to live, and an editor that
// puts them only on toolbar buttons is one where closing a panel is permanent.
//
// Drawn BEFORE the dockspace, because `DockSpaceOverViewport` measures the work
// area and a menu bar declared after it would overlap the panels by its own
// height.
void drawMenuBar(Editor& editor, EditorPanels& panels, EditorCommands& commands, EditorDialogs& dialogs)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        // **Anything that would throw work away asks first**, and it asks in one
        // place: `dialogs.pending` remembers which door was used and the answer
        // re-issues it. Without work to lose there is nothing to ask about, and
        // a dialog that appeared anyway would be a dialog people learn to
        // dismiss without reading.
        const auto guard = [&](EditorDialogs::Pending what) {
            if (!editor.hasUnsavedWork())
                return false;
            dialogs.pending = what;
            return true;
        };

        // **A project is a process** (ADR 0055), so both of these start the
        // browser and close this editor rather than swapping a project inside a
        // running one. The browser is where a project is made and where one is
        // picked, which is why File has no second copy of either.
        if (ImGui::MenuItem("New Project...")) {
            if (!guard(EditorDialogs::Pending::NewProject))
                commands.newProject = true;
        }
        if (ImGui::MenuItem("Open Project...")) {
            if (!guard(EditorDialogs::Pending::OpenProject))
                commands.openProject = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New Scene")) {
            if (!guard(EditorDialogs::Pending::NewScene))
                commands.newScene = true;
        }
        ImGui::Separator();
        // **Ctrl+S saves whatever is being edited**, and on a stamp stage that
        // is the stamp. One shortcut rather than two, because "save what I am
        // looking at" is the only thing a person means by it -- and a Ctrl+S
        // that wrote the scene while somebody was editing a prefab would save
        // the wrong document without saying so.
        const bool stampOpen = editor.stampSession().open();
        if (stampOpen) {
            if (ImGui::MenuItem("Save Stamp", "Ctrl+S"))
                commands.saveStamp = true;
        }
        else {
            // Greyed rather than hidden when there is nothing to save to: the
            // item has to be where somebody expects it even when it cannot act,
            // or they conclude the editor cannot do it at all.
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !editor.openScenePath().empty()))
                commands.save = true;
        }
        if (ImGui::MenuItem("Save Scene As..."))
            dialogs.saveAs = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            if (!guard(EditorDialogs::Pending::Quit))
                commands.quit = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        // Named after what they will undo. "Undo" alone leaves somebody to find
        // out by pressing it, which for a delete is finding out too late.
        const std::string undoLabel =
            editor.history().canUndo() ? "Undo " + std::string(editor.history().undoLabel()) : std::string("Undo");
        const std::string redoLabel =
            editor.history().canRedo() ? "Redo " + std::string(editor.history().redoLabel()) : std::string("Redo");

        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, editor.history().canUndo()))
            commands.undo = true;
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y / Ctrl+Shift+Z", false, editor.history().canRedo()))
            commands.redo = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Preferences..."))
            dialogs.preferences = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Explorer", nullptr, &panels.explorer);
        ImGui::MenuItem("Properties", nullptr, &panels.properties);
        ImGui::MenuItem("Viewport", nullptr, &panels.viewport);
        ImGui::MenuItem("Content", nullptr, &panels.content);
        ImGui::MenuItem("Console", nullptr, &panels.console);
        ImGui::MenuItem("Stats", nullptr, &panels.stats);
        ImGui::Separator();
        // Not a panel, but it is a question about what the panels SHOW, and
        // this is the menu a person opens to ask one.
        ImGui::MenuItem("Streamed Content", nullptr, &panels.showGenerated);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("show what streaming materialised. It is not part of the scene: the save skips it and "
                              "nothing authored can live in it");
        }
        ImGui::Separator();
        // Not "close everything": somebody who has lost a panel behind another
        // wants the arrangement back, not an empty window.
        if (ImGui::MenuItem("Reset Layout"))
            commands.resetLayout = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About LuauG"))
            dialogs.about = true;
        ImGui::EndMenu();
    }

    // Which scene this is, right-aligned. The menu bar is the best-placed thing
    // to answer that, and it is the question somebody asks just before saving
    // over something.
    const std::string open = editor.openScenePath().empty() ? std::string("untitled") : editor.openScenePath();
    const float width = ImGui::CalcTextSize(open.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::TextDisabled("%s", open.c_str());

    ImGui::EndMainMenuBar();
}

// The dialogs, at the shell's level rather than inside a panel.
//
// A popup belongs to the window that opened it, so one living inside the
// viewport could not be opened from the menu -- which is exactly what a Save As
// has to be. Modal, because each of these is a question with an answer, and one
// left half-answered behind a panel is one somebody loses track of.
void drawEditorDialogs(Editor& editor, EditorCommands& commands, EditorDialogs& dialogs, IconAtlas* icons)
{
    if (dialogs.saveAs) {
        dialogs.saveAs = false;
        ImGui::OpenPopup("Save Scene As");
    }
    if (dialogs.preferences) {
        dialogs.preferences = false;
        ImGui::OpenPopup("Preferences");
    }
    if (dialogs.about) {
        dialogs.about = false;
        ImGui::OpenPopup("About LuauG");
    }

    ImGui::SetNextWindowSize(ImVec2(470.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_NoResize)) {
        static std::array<char, 160> path{};
        ImGui::TextDisabled("Scenes live under the project's content directory.");
        ImGui::Spacing();

        ImGui::TextUnformatted("content/");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetNextItemWidth(330.0f);
        const bool submitted =
            ImGui::InputText("##scene-path", path.data(), path.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string typed(path.data());

        // The RESOLVED path, not the typed one. The label above the box already
        // says `content/`, so `content/scenes/main` is the natural thing to
        // type and used to be saved verbatim into `content/content/` -- D068,
        // found by the first person to use this dialog. Showing what will
        // actually be written is what makes the normalisation visible instead
        // of surprising.
        const std::string resolved = Editor::normalizeScenePath(typed);
        const bool usable = !typed.empty() && Editor::sceneNameIsUsable(resolved);
        if (usable)
            ImGui::TextDisabled("saves as content/%s", resolved.c_str());
        else if (typed.empty())
            ImGui::TextDisabled("saves as content/…");
        else
            ImGui::TextDisabled("not a path inside content/");

        ImGui::Spacing();
        ImGui::BeginDisabled(!usable);
        const bool accepted = ImGui::Button("Save", ImVec2(120.0f, 0.0f));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            path.fill(0);
            ImGui::CloseCurrentPopup();
        }

        if ((submitted || accepted) && usable) {
            commands.saveAs = typed;
            path.fill(0);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(470.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Preferences", nullptr, ImGuiWindowFlags_NoResize)) {
        // Deliberately small and deliberately real. An empty preferences window
        // is a promise; one holding the setting somebody actually reaches for is
        // a place the next setting knows where to go.
        // **First, because it is the one setting that changes what every other
        // panel looks like** -- and because a person who came here looking for
        // one thing came looking for this.
        ImGui::SeparatorText("Appearance");
        const Theme& current = themeById(g_appearance.themeId);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("Theme", std::string(current.name).c_str())) {
            for (const Theme& theme : themes()) {
                const bool selected = theme.id == current.id;
                if (ImGui::Selectable(std::string(theme.name).c_str(), selected)) {
                    g_appearance.themeId = std::string(theme.id);
                    // Applied on the spot rather than on Close: a theme you
                    // have to dismiss a dialog to see is a theme you choose by
                    // trial and error.
                    applyAppearance();
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Shown resolved rather than as the stored zero, so the slider says what
        // the shell is actually drawn at. Committed on release: dragging it
        // rewrites the whole style every frame, and writing the file that often
        // is a file write per pixel of travel.
        f32 scale = resolveUiScale(g_appearance.scale, g_displayScale);
        if (ImGui::SliderFloat("Interface scale", &scale, kMinimumUiScale, kMaximumUiScale, "%.2fx")) {
            g_appearance.scale = scale;
            applyTheme(themeById(g_appearance.themeId), resolveUiScale(scale, g_displayScale));
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            applyAppearance();
        ImGui::SameLine();
        if (ImGui::Button("Match display")) {
            // Zero is the stored spelling of "ask the display", which is what
            // this button puts back -- not the number the display happens to
            // report today, because that one is wrong the moment somebody
            // changes monitors.
            g_appearance.scale = 0.0f;
            applyAppearance();
        }
        ImGui::TextDisabled("This display reports %.2fx. Theme and scale are yours, not the project's.",
                            static_cast<double>(g_displayScale));

        ImGui::Spacing();
        ImGui::SeparatorText("Viewport");
        f32 speed = editor.cameraSpeed();
        if (ImGui::DragFloat("Camera speed (m/s)", &speed, 0.5f, 0.1f, 2000.0f, "%.1f"))
            editor.setCameraSpeed(speed);
        ImGui::TextDisabled("The scroll wheel changes this while flying, too.");

        ImGui::Spacing();
        ImGui::SeparatorText("Icons");
        if (icons != nullptr) {
            bool tinting = icons->tinting();
            if (ImGui::Checkbox("Colour icons by role", &tinting))
                icons->setTinting(tinting);
            // **Not a degraded mode, and it says so.** The set was drawn and
            // collision-checked in a single ink before any colour existed, so
            // somebody who cannot use the colours is not being handed a worse
            // tool -- which is the reason this switch exists at all.
            ImGui::TextWrapped("The colour says what KIND of thing an icon is, never which thing -- the shape carries "
                               "that. Off, every icon takes the panel's own foreground, and no two of them are closer "
                               "than the set was drawn to be.");
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Where these live");
        ImGui::TextWrapped("The panel layout and the scene you had open are per-person and are kept in the project's "
                           ".luaug directory. What a RUN of the project starts with is the project's own decision, "
                           "and lives in luaug.toml. The theme and the interface scale are neither: they are about "
                           "your eyes and your monitor, so they follow you across every project and live in your own "
                           "user directory.");

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (dialogs.renameInstance || dialogs.renameContent) {
        ImGui::OpenPopup("Rename");
    }
    if (dialogs.newFolder) {
        dialogs.newFolder = false;
        ImGui::OpenPopup("New Folder");
    }
    if (dialogs.newStamp) {
        dialogs.newStamp = false;
        ImGui::OpenPopup("New Stamp");
    }
    if (!dialogs.deleteContentPath.empty())
        ImGui::OpenPopup("Delete");
    // The window's own close button arrives as a platform event, so the frame
    // loop raises it on the editor and this is where it becomes a question.
    if (editor.closeRequested() && dialogs.pending == EditorDialogs::Pending::None)
        dialogs.pending = EditorDialogs::Pending::Quit;
    if (dialogs.pending != EditorDialogs::Pending::None)
        ImGui::OpenPopup("Unsaved Changes");

    ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_NoResize)) {
        static std::array<char, 128> name{};
        // Seeded once, on the frame the dialog opens. Copying every frame would
        // overwrite what the person is typing with what they started from.
        if (dialogs.renameInstance || dialogs.renameContent) {
            name.fill(0);
            const std::size_t count = std::min(dialogs.renameSeed.size(), name.size() - 1);
            std::memcpy(name.data(), dialogs.renameSeed.data(), count);
            dialogs.renameInstance = false;
            dialogs.renameContent = false;
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SetKeyboardFocusHere();
        const bool submitted =
            ImGui::InputText("##rename", name.data(), name.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string typed(name.data());

        ImGui::Spacing();
        ImGui::BeginDisabled(typed.empty());
        const bool accepted = ImGui::Button("Rename", ImVec2(120.0f, 0.0f));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            dialogs.renameTarget = {};
            dialogs.renameContentPath.clear();
            ImGui::CloseCurrentPopup();
        }

        if ((submitted || accepted) && !typed.empty()) {
            if (dialogs.renameTarget.valid()) {
                commands.renameInstance = dialogs.renameTarget;
                commands.renameInstanceTo = typed;
            }
            else if (!dialogs.renameContentPath.empty()) {
                commands.renameContent = dialogs.renameContentPath;
                commands.renameContentTo = typed;
            }
            dialogs.renameTarget = {};
            dialogs.renameContentPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_NoResize)) {
        static std::array<char, 128> folder{};
        ImGui::SetNextItemWidth(-1.0f);
        const bool submitted =
            ImGui::InputText("##folder", folder.data(), folder.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string typed(folder.data());

        ImGui::Spacing();
        // Greyed before the press rather than refused after it: a name a
        // filesystem cannot carry is knowable while it is being typed.
        ImGui::BeginDisabled(!ContentTree::isUsableName(typed));
        const bool accepted = ImGui::Button("Create", ImVec2(120.0f, 0.0f));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            folder.fill(0);
            ImGui::CloseCurrentPopup();
        }

        if ((submitted || accepted) && ContentTree::isUsableName(typed)) {
            commands.createFolder = typed;
            folder.fill(0);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Stamp", nullptr, ImGuiWindowFlags_NoResize)) {
        static std::array<char, 128> stamp{};
        // **What this does, in one sentence, because the second half surprises
        // people**: it writes a file AND turns the thing you made it from into
        // an instance of that file. A source plus a copy of it that nothing
        // connects is two things that drift apart by tomorrow.
        ImGui::TextDisabled("Writes the selected subtree to a file, and this instance becomes one of its stamps.");
        ImGui::Spacing();
        ImGui::TextUnformatted("content/");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetNextItemWidth(-1.0f);
        const bool submitted =
            ImGui::InputText("##stamp", stamp.data(), stamp.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string typed(stamp.data());
        const bool usable = !typed.empty() && Editor::stampNameIsUsable(typed);

        ImGui::Spacing();
        // The RESOLVED path while it is being typed, which is the half that
        // makes a rule visible rather than surprising -- D068 is what happens
        // without it.
        if (usable)
            ImGui::TextDisabled("writes content/%s", Editor::normalizeStampPath(typed).c_str());
        else if (typed.empty())
            ImGui::TextDisabled("a bare name lands in content/stamps/");
        else
            ImGui::TextDisabled("not a name a file can have");

        ImGui::Spacing();
        ImGui::BeginDisabled(!usable || !dialogs.stampSubject.valid());
        const bool accepted = ImGui::Button("Create", ImVec2(120.0f, 0.0f));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            stamp.fill(0);
            dialogs.stampSubject = {};
            ImGui::CloseCurrentPopup();
        }

        if ((submitted || accepted) && usable && dialogs.stampSubject.valid()) {
            commands.stampSubject = dialogs.stampSubject;
            commands.stampName = typed;
            stamp.fill(0);
            dialogs.stampSubject = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Delete", nullptr, ImGuiWindowFlags_NoResize)) {
        // **Named, not counted.** "Delete 1 item?" is a question nobody can
        // answer; the path is what tells somebody whether they meant it.
        ImGui::TextWrapped("Delete content/%s?", dialogs.deleteContentPath.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("This removes the file from disk. A folder goes with everything in it, and there is no "
                            "undo for either.");
        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
            commands.deleteContent = dialogs.deleteContentPath;
            dialogs.deleteContentPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            dialogs.deleteContentPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // **The question every application with a document asks**, asked in one
    // place: closing, starting over, opening something else and leaving for
    // another project all lose the same edits, so `dialogs.pending` remembers
    // which of them asked and the answer re-issues it.
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_NoResize)) {
        const bool stampOpen = editor.stampSession().open();
        const bool haveSomewhereToSave = stampOpen || !editor.openScenePath().empty();

        if (stampOpen) {
            ImGui::TextWrapped("Save changes to %s?", editor.stampSession().path.c_str());
        }
        else if (!editor.openScenePath().empty()) {
            ImGui::TextWrapped("Save changes to %s?", editor.openScenePath().c_str());
        }
        else {
            // An untitled scene has nowhere to go, so the honest question is a
            // different one and the buttons below say so.
            ImGui::TextWrapped("This scene has never been saved.");
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Discarding loses every edit since the last save, and there is no undo for that.");
        ImGui::Spacing();

        // Three answers and no more, in the order every application puts them:
        // keep the work, throw it away, or change your mind.
        const auto proceed = [&]() {
            switch (dialogs.pending) {
            case EditorDialogs::Pending::Quit:
                commands.quit = true;
                break;
            case EditorDialogs::Pending::NewScene:
                commands.newScene = true;
                break;
            case EditorDialogs::Pending::OpenScene:
                commands.openScene = dialogs.pendingScene;
                break;
            case EditorDialogs::Pending::NewProject:
                commands.newProject = true;
                break;
            case EditorDialogs::Pending::OpenProject:
                commands.openProject = true;
                break;
            case EditorDialogs::Pending::None:
                break;
            }
            dialogs.pending = EditorDialogs::Pending::None;
            dialogs.pendingScene.clear();
            editor.clearCloseRequest();
            ImGui::CloseCurrentPopup();
        };

        if (ImGui::Button(haveSomewhereToSave ? "Save" : "Save As...", ImVec2(130.0f, 0.0f))) {
            if (stampOpen) {
                commands.saveStamp = true;
                proceed();
            }
            else if (haveSomewhereToSave) {
                commands.save = true;
                proceed();
            }
            else {
                // **The name has to be typed before anything can be written**,
                // and what was asked for is dropped rather than queued behind a
                // second dialog: a person who has just been asked where to save
                // will ask again for whatever they wanted, and an action that
                // fired after an unrelated box closed would be a surprise.
                commands.wantSaveAs = true;
                dialogs.pending = EditorDialogs::Pending::None;
                dialogs.pendingScene.clear();
                editor.clearCloseRequest();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(130.0f, 0.0f)))
            proceed();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(130.0f, 0.0f))) {
            dialogs.pending = EditorDialogs::Pending::None;
            dialogs.pendingScene.clear();
            editor.clearCloseRequest();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("About LuauG", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("LuauG");
        ImGui::TextDisabled("An open-source game engine scripted in Luau.");
        ImGui::Spacing();
        ImGui::TextDisabled("Apache-2.0. The editor is post-v1 phase 1.");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// The editor's furniture. The panels inside it are the overlay's own, which is
// the whole argument of ADR 0046: what an editor mostly is, this engine already
// had.
void drawEditorShell(const Frame& frame, scene::World* world, core::InstanceId root, Inspector* inspector,
                     script::ScriptRuntime* runtime, Editor* editor, rhi::TextureHandle viewport, bool& laidOut,
                     EditorCommands& commands, EditorPanels& panels, EditorDialogs& dialogs, IconAtlas* icons,
                     bool furniture)
{
    // **F3 down: the world and nothing else.** Returning before the dockspace
    // rather than hiding each panel, because a dockspace with no windows in it
    // is still a dockspace and would draw its own background over the picture.
    if (!furniture) {
        if (editor != nullptr)
            drawViewportFullscreen(*editor, viewport, commands);
        return;
    }

    // Before the dockspace. `DockSpaceOverViewport` measures the work area, and
    // a menu bar declared after it would sit on top of the panels by its own
    // height.
    if (editor != nullptr)
        drawMenuBar(*editor, panels, commands, dialogs);

    // A transparent central node, so a layout that has not been built yet shows
    // the frame underneath instead of a slab of grey.
    const ImGuiID dockspace =
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // `DockBuilderGetNode` answers null until the dockspace exists, and a saved
    // layout has already put windows into it by the time it does -- so "nobody
    // has arranged this yet" is the node having no split and no window, which is
    // exactly the state a first launch is in.
    bool builtThisFrame = false;
    if (!laidOut || commands.resetLayout) {
        const bool asked = commands.resetLayout;
        commands.resetLayout = false;
        laidOut = true;
        // **The browser opens the way it was left**, which the panel struct
        // cannot answer by itself -- it is rebuilt from nothing every launch.
        // Seeded here rather than at construction because this is the first
        // frame that has an editor in hand.
        if (!asked && editor != nullptr)
            panels.contentView = editor->contentView();
        const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
        // Asked for, or never arranged. `DockBuilderRemoveNode` throws away an
        // arrangement somebody chose, so it only runs when they said so or when
        // there is nothing to throw away.
        if (asked || node == nullptr || (!node->IsSplitNode() && node->Windows.Size == 0)) {
            buildDefaultLayout(dockspace);
            builtThisFrame = true;
            if (asked) {
                panels = EditorPanels{};
                // Reset Layout means the arrangement, and the browser's layout
                // is part of it -- so the remembered one goes with it rather
                // than coming back on the next launch.
                if (editor != nullptr)
                    editor->setContentView(panels.contentView);
            }
        }
    }

    if (editor != nullptr) {
        if (panels.viewport)
            drawViewport(*editor, viewport, commands, panels.viewport, icons);
        if (panels.content)
            drawContent(*editor, commands, panels, dialogs, icons);
    }

    if (panels.explorer) {
        if (ImGui::Begin("Explorer", &panels.explorer)) {
            if (world != nullptr && inspector != nullptr) {
                // **While a stamp is open the tree is the STAMP's**, root row
                // and all: no services, no scene, nothing but what is in the
                // file. That is the whole of "a separate environment" as far as
                // the Explorer is concerned, and the viewport already shows the
                // same thing because opening cleared the scene out of the world.
                const bool editingStamp = editor != nullptr && editor->stampSession().open();
                const core::InstanceId treeRoot = editingStamp ? editor->stampSession().root : root;

                // **Two trees, and the tabs are what says they are two.** The
                // scene is what this world holds; `Content` is what the PROJECT
                // holds, global to every scene in it (ADR 0052). Instance
                // inside instance in both, the same verbs in both -- and
                // nothing in `Content` runs, which is the one sentence that
                // explains the difference.
                drawExplorer(*world, treeRoot, *inspector, &commands, &dialogs, icons, panels.showGenerated,
                             editingStamp, editor != nullptr && editor->hasClipboard());
            }
        }
        ImGui::End();
    }

    if (panels.properties) {
        if (ImGui::Begin("Properties", &panels.properties)) {
            if (world != nullptr && inspector != nullptr) {
                drawProperties(*world, *inspector, editor != nullptr ? &editor->content() : nullptr);
                drawWriteLog(*world, *inspector);
            }
        }
        ImGui::End();
    }

    // Draws with no VM for the same reason it does in the overlay: the LOG half
    // is what somebody wants when the VM failed to boot.
    if (panels.console) {
        if (ImGui::Begin("Console", &panels.console))
            drawConsole(runtime);
        ImGui::End();
    }

    if (panels.stats) {
        if (ImGui::Begin("Stats", &panels.stats)) {
            drawStats(frame);
            if (runtime != nullptr)
                drawMemory(*runtime);
        }
        ImGui::End();
    }

    // **Escape lets go of the selection, from anywhere.** Deselecting is a thing
    // a person does constantly and it needs a key that works wherever they are
    // looking -- the explorer, the viewport, the content browser.
    //
    // Only when nothing else has a claim on it, and the order matters: a modal
    // is closed by Escape and a text field cancels its edit with it, and taking
    // the key from either would make the shell's own dialogs unclosable. So it
    // is asked for last, after everything that could have wanted it.
    const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() && !popupOpen) {
        // **In play mode Escape stops**, which is what Unreal does and what
        // this editor now needs rather than merely wants. A running game holds
        // the pointer, and while it does the panels do not see the mouse at all
        // (D069) -- so a project that never hands the cursor back would have no
        // way to reach the stop button. The keyboard still arrives, and a
        // transport somebody cannot reach is not a transport.
        //
        // The game sees the key too. That is the same arrangement Unreal ships
        // and the honest one: while you are testing, the tool keeps one key.
        if (editor != nullptr && editor->inPlayMode())
            commands.play = false;
        else
            commands.clearSelection = true;
    }

    // **W, E, R for the three manipulators**, which is what every editor in this
    // shape uses and therefore what somebody's hands already know. They do not
    // collide with the fly camera: that reads WASD only while the right button
    // is held, and these are refused while it is.
    //
    // Alt suspends the grid for as long as it is held. That way round because
    // the number somebody wants is far more often a round one, so the modifier
    // is for the exception rather than for the rule.
    if (editor != nullptr && !ImGui::IsAnyItemActive() && !popupOpen && !editor->lookInput().active) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            editor->setGizmoMode(GizmoMode::Translate);
        if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            editor->setGizmoMode(GizmoMode::Rotate);
        if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            editor->setGizmoMode(GizmoMode::Scale);

        // **F frames the selection**, which is the one camera shortcut every
        // editor in this shape shares -- Studio, Unity, Unreal and Blender all
        // put it here, so it is what somebody's hands already reach for.
        //
        // Nothing happens with nothing selected, and nothing happens for a
        // selection with no extent: a `Folder` has a position and no size, and
        // "showing" it would move the view somewhere arbitrary.
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && world != nullptr && inspector != nullptr) {
            core::DVec3 centre;
            core::f64 radius = 0.0;
            if (selectionBounds(*world, inspector->selectionSet(), centre, radius))
                editor->focusCamera(centre, radius);
        }
    }
    if (editor != nullptr)
        editor->setSnapSuspended(ImGui::GetIO().KeyAlt);

    // Ctrl+Z and Ctrl+Y, under the same rule Escape is: not while a field has
    // the keyboard, because Ctrl+Z inside a text box is the box's own undo and
    // taking it would make typing a name unrecoverable.
    if (!ImGui::IsAnyItemActive() && !popupOpen && ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            // Ctrl+Shift+Z is redo everywhere except Windows, and on Windows it
            // is redo as well as Ctrl+Y -- so both work and nobody has to learn
            // which half of their habits this editor kept.
            if (ImGui::GetIO().KeyShift)
                commands.redo = true;
            else
                commands.undo = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            commands.redo = true;
        // **Ctrl+S saves what is open**, which is a stamp while one is and the
        // scene otherwise. One key, because "save" is one intention and the
        // person pressing it is not thinking about which document it reaches.
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            if (editor != nullptr && editor->stampSession().open())
                commands.saveStamp = true;
            else if (editor != nullptr && !editor->openScenePath().empty())
                commands.save = true;
            else
                commands.wantSaveAs = true;
        }
    }

    // --- Delete, F2 and Ctrl+D ---------------------------------------------
    //
    // **The three every editor has**, on the keys every editor puts them on, so
    // that hands already know them. Under the same guard as the rest: not while
    // a field has the keyboard, because Delete in a text box is a character and
    // F2 in one is nothing.
    //
    // **And not while playing.** A running world is one `stop` is about to put
    // back, so an edit made in it is work about to be thrown away without a
    // word -- which is worse than a key that does nothing.
    const bool authoring = editor != nullptr && world != nullptr && inspector != nullptr && !ImGui::IsAnyItemActive() &&
                           !popupOpen && !editor->inPlayMode();

    // **Paste needs no selection**, because pasting into an empty world is
    // exactly what somebody does after copying out of another one. Everything
    // else acts ON something and is guarded below.
    if (authoring && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        // Shift is the difference between beside and inside, which is the one
        // thing about a paste anybody has to remember.
        if (ImGui::GetIO().KeyShift)
            commands.pasteInto = true;
        else
            commands.paste = true;
    }

    if (authoring && inspector->selectionCount() > 0) {
        const bool engineOwned = Editor::isEngineOwned(*world, inspector->selection(), root);

        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !engineOwned)
            commands.deleteSelection = true;

        if (ImGui::GetIO().KeyCtrl && !engineOwned) {
            if (ImGui::IsKeyPressed(ImGuiKey_D, false))
                commands.duplicateSelection = true;
            if (ImGui::IsKeyPressed(ImGuiKey_C, false))
                commands.copySelection = true;
            if (ImGui::IsKeyPressed(ImGuiKey_X, false))
                commands.cutSelection = true;
        }

        // F2 opens the box on the PRIMARY, because renaming four things to one
        // name is not a thing anybody means -- which is the same reason the
        // menu item beside it is singular.
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && !engineOwned) {
            dialogs.renameTarget = inspector->selection();
            dialogs.renameContentPath.clear();
            dialogs.renameSeed = std::string(world->atoms().text(world->name(inspector->selection())));
            dialogs.renameInstance = true;
        }
    }

    if (commands.wantSaveAs) {
        commands.wantSaveAs = false;
        dialogs.saveAs = true;
    }
    if (editor != nullptr)
        drawEditorDialogs(*editor, commands, dialogs, icons);

    // After every panel has been declared, because focusing a window ImGui has
    // not seen this frame does nothing. Only on the frame the default layout
    // was built: a person who later chose the console should find the console,
    // and the ini remembers which tab that was.
    //
    // **Properties before content**, and the order is the point twice over: each
    // call selects the tab in its OWN node, so both nodes get the tab they
    // should open on, and the last call is the one that also takes keyboard
    // focus -- which belongs to the panel somebody is about to browse rather
    // than to the one they are about to read.
    if (builtThisFrame) {
        ImGui::SetWindowFocus("Properties");
        ImGui::SetWindowFocus("Content");
    }
}

// --- the streaming map ------------------------------------------------------
//
// **The panel `api-design.md` has promised since M7 and nothing drew.**
// `StreamingManager::view` was written "for the overlay the deliverable owes"
// and had no caller in the tree until this; the manual page told people to open
// a panel that did not exist. It exists now, and E5's gate is a picture of it.
//
// A MAP rather than a table, because the question a person has about streaming
// is spatial: which cells are here, which are on their way, and how far out the
// ring reaches. A table of chunk ids answers none of that at a glance.

[[nodiscard]] ImU32 chunkStateColor(asset::ChunkState state)
{
    // Through the palette, so the map is legible in both themes -- the colours
    // it carried were picked against a dark ground and three of the five were
    // invisible on a light one.
    const ThemePalette& p = palette();
    switch (state) {
    case asset::ChunkState::Resident:
        return ImGui::ColorConvertFloat4ToU32(ImVec4(p.success.r, p.success.g, p.success.b, 0.90f));
    case asset::ChunkState::Decoded:
        return ImGui::ColorConvertFloat4ToU32(ImVec4(p.warning.r, p.warning.g, p.warning.b, 0.90f));
    case asset::ChunkState::Loading:
        return ImGui::ColorConvertFloat4ToU32(ImVec4(p.accent.r, p.accent.g, p.accent.b, 0.90f));
    case asset::ChunkState::Failed:
        return ImGui::ColorConvertFloat4ToU32(ImVec4(p.danger.r, p.danger.g, p.danger.b, 0.90f));
    case asset::ChunkState::Unloaded:
        break;
    }
    return ImGui::ColorConvertFloat4ToU32(ImVec4(p.border.r, p.border.g, p.border.b, 0.70f));
}

void drawStreaming(const StreamingHost& streaming)
{
    const asset::StreamingStats& stats = streaming.stats();
    ImGui::Text("resident %u  loading %u  decoded %u  failed %u", stats.resident, stats.loading, stats.decoded,
                stats.failed);
    ImGui::Text("%.2f MiB resident, %llu loaded, %llu evicted", static_cast<double>(stats.bytesResident) / 1048576.0,
                static_cast<unsigned long long>(stats.chunksLoaded),
                static_cast<unsigned long long>(stats.chunksEvicted));
    ImGui::Text("worst pump %.2f ms, last %.2f ms, %llu rebase(s)", stats.worstTickMs, streaming.lastPumpMilliseconds(),
                static_cast<unsigned long long>(streaming.rebases()));

    const std::vector<asset::StreamingManager::ChunkView> cells = streaming.view();
    if (cells.empty())
        return;

    // One map per size class, because that is what a layer IS (ADR 0053) and
    // because two classes drawn on one grid would put a pebble's cell on top of
    // a hillside's. The gate's own item -- "a large object stays resident at a
    // distance that has already evicted a small one" -- is a thing you read off
    // these two pictures side by side.
    for (core::i32 layer = 0; layer < asset::ChunkLayerCount; ++layer) {
        core::i32 minX = 0;
        core::i32 maxX = 0;
        core::i32 minZ = 0;
        core::i32 maxZ = 0;
        core::u32 present = 0;
        core::u32 resident = 0;
        for (const asset::StreamingManager::ChunkView& cell : cells) {
            if (cell.id.layer != layer)
                continue;
            if (present == 0) {
                minX = maxX = cell.id.x;
                minZ = maxZ = cell.id.z;
            }
            minX = std::min(minX, cell.id.x);
            maxX = std::max(maxX, cell.id.x);
            minZ = std::min(minZ, cell.id.z);
            maxZ = std::max(maxZ, cell.id.z);
            ++present;
            if (cell.state == asset::ChunkState::Resident)
                ++resident;
        }
        if (present == 0)
            continue;

        static constexpr const char* kLayerNames[] = {"detail", "structures", "terrain"};
        ImGui::SeparatorText(layer < 3 ? kLayerNames[layer] : "layer");
        ImGui::Text("%u of %u resident", resident, present);

        const int columns = maxX - minX + 1;
        const int rows = maxZ - minZ + 1;
        // Capped so a 17x17 world and a 200x200 one both fit a panel. A cell
        // smaller than three pixels is a colour nobody can read, and a map
        // wider than the panel is one nobody can see the edge of.
        const float side = std::clamp(220.0f / static_cast<float>(std::max(columns, rows)), 3.0f, 18.0f);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (const asset::StreamingManager::ChunkView& cell : cells) {
            if (cell.id.layer != layer)
                continue;
            const float x = origin.x + static_cast<float>(cell.id.x - minX) * side;
            // Z DOWN the screen, which is the reading a map wants: north at the
            // top is a convention, and a grid drawn with +z upward reads
            // mirrored against every other view of the same world.
            const float y = origin.y + static_cast<float>(cell.id.z - minZ) * side;
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + side - 1.0f, y + side - 1.0f), chunkStateColor(cell.state));
        }
        ImGui::Dummy(ImVec2(static_cast<float>(columns) * side, static_cast<float>(rows) * side));
    }
}

// --- The project browser (ADR 0055) ------------------------------------------
//
// **One window filling the screen, and no dockspace.** The editor's shell is
// arrangeable because somebody works in it for hours; this is a screen you look
// at once per session and leave, and a launcher whose panels can be dragged
// apart is a launcher somebody can break.
//
// Every decision it takes is written into the `LauncherView` the caller owns and
// acted on by the loop, for the reason `EditorCommands` exists: starting a
// process from inside an ImGui callback is a frame that never finishes drawing.

// What the person is typing. Held here rather than in the view, because it is
// the panel's own state in exactly the way the explorer's expanded set is: the
// loop has no use for a half-typed name.
struct LauncherForm
{
    // Sized rather than dynamic, because ImGui's text field takes a buffer. Long
    // enough for a path somebody would actually type.
    char name[64]{};
    char parent[512]{};
    char open[512]{};
    int templateIndex = 0;
    bool creating = false;
    bool seeded = false;
};

LauncherForm g_launcherForm;
std::vector<std::string> g_launcherTemplates;

void copyInto(char* buffer, std::size_t size, std::string_view text)
{
    const std::size_t count = std::min(text.size(), size - 1);
    std::memcpy(buffer, text.data(), count);
    buffer[count] = '\0';
}

// --- Small pieces the two columns share --------------------------------------

// A quiet label over a block. With no rounding and no cards, a label and the
// space under it are what make a group read as one thing -- which is the whole
// technique this screen is drawn with.
void sectionLabel(const char* text)
{
    ImGui::TextDisabled("%s", text);
    ImGui::Spacing();
}

// **The one button on a screen that wears the accent.** Everything else is a
// surface, for the reason `applyTheme` states: spend the brand colour on every
// button and it stops saying anything.
//
// **And it takes the accent off when it cannot act**, rather than relying on
// `BeginDisabled`'s alpha. A dimmed brand colour is still the brand colour, so a
// disabled primary drawn that way reads as the thing to press -- which is the
// one place on this screen where being wrong costs somebody a click into
// nothing. Refusing looks like a surface; the caller still wraps it in
// `BeginDisabled`, which is what makes it actually refuse.
bool primaryButton(const char* label, ImVec2 size, bool enabled)
{
    const ThemePalette& p = palette();
    const core::Color3 face = enabled ? p.accent : p.surfaceRaised;
    ImGui::PushStyleColor(ImGuiCol_Button, themeColor(face));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, themeBlend(face, p.text, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, themeBlend(face, p.onAccent, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_Text, themeColor(enabled ? p.onAccent : p.textMuted));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

// The list, and the two things a row can do.
void drawLauncherProjects(LauncherView& view)
{
    ProjectList& projects = *view.projects;
    if (projects.entries().empty()) {
        // Inside the same bordered box the list would fill, rather than as a
        // bare line where the box would have been: an empty state that changes
        // the shape of the screen reads as something having gone wrong.
        if (ImGui::BeginChild("##projects", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
            ImGui::Spacing();
            ImGui::TextDisabled("No projects yet.");
            ImGui::TextDisabled("Make one on the right, or open a folder you already have.");
        }
        ImGui::EndChild();
        return;
    }

    const float rowHeight = ImGui::GetFrameHeight() * 2.0f;
    if (ImGui::BeginChild("##projects", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        std::filesystem::path forget;
        for (const RecentProject& entry : projects.entries()) {
            ImGui::PushID(entry.path.string().c_str());

            const ImVec2 origin = ImGui::GetCursorPos();
            // The whole row is the target, so opening a project is a click
            // anywhere on it rather than a hunt for a button.
            if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, rowHeight))) {
                if (entry.missing)
                    view.message = entry.path.string() + " is not there any more.";
                else
                    view.open = entry.path;
            }

            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 rowMin = ImGui::GetItemRectMin();
            const ImVec2 rowMax = ImGui::GetItemRectMax();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            // **A bar on the left rather than a tint across the row.** A square
            // list has no shape of its own to mark, and a filled row competes
            // with the selection colour the rest of the shell already spends
            // the accent on.
            if (hovered) {
                draw->AddRectFilled(rowMin, ImVec2(rowMin.x + 3.0f * ImGui::GetStyle().FontScaleMain, rowMax.y),
                                    ImGui::ColorConvertFloat4ToU32(themeColor(palette().accent)));
            }
            // One hairline per row, which is what separates rows when nothing is
            // rounded and nothing is filled.
            draw->AddLine(ImVec2(rowMin.x, rowMax.y), ImVec2(rowMax.x, rowMax.y),
                          ImGui::ColorConvertFloat4ToU32(themeColor(palette().border)));

            const float textX = origin.x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPos(ImVec2(textX, origin.y + ImGui::GetStyle().FramePadding.y));
            if (entry.missing) {
                ImGui::TextDisabled("%s", entry.name.c_str());
                ImGui::SetCursorPos(ImVec2(textX, origin.y + ImGui::GetTextLineHeightWithSpacing()));
                // Said out loud, and the row stays. A list that edits itself
                // when a drive is unplugged is a list nobody can trust.
                ImGui::TextColored(themeColor(palette().warning), "missing - %s", entry.path.string().c_str());
            }
            else {
                ImGui::TextUnformatted(entry.name.c_str());
                ImGui::SetCursorPos(ImVec2(textX, origin.y + ImGui::GetTextLineHeightWithSpacing()));
                ImGui::TextDisabled("%s", entry.path.string().c_str());
            }

            if (hovered) {
                const float buttonWidth = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttonWidth);
                ImGui::SetCursorPosY(origin.y + (rowHeight - ImGui::GetFrameHeight()) * 0.5f);
                if (ImGui::SmallButton("Remove"))
                    forget = entry.path;
            }

            ImGui::SetCursorPos(ImVec2(origin.x, origin.y + rowHeight));
            ImGui::PopID();
        }
        if (!forget.empty())
            projects.forget(forget);
    }
    ImGui::EndChild();
}

void drawLauncherNew(LauncherView& view)
{
    LauncherForm& form = g_launcherForm;

    if (g_launcherTemplates.empty()) {
        // Said rather than shown as a disabled button nobody can explain.
        ImGui::TextColored(themeColor(palette().warning), "No templates found under %s.",
                           view.templatesDir.string().c_str());
        return;
    }

    ImGui::TextUnformatted("Template");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##template", g_launcherTemplates[static_cast<std::size_t>(form.templateIndex)].c_str())) {
        for (int i = 0; i < static_cast<int>(g_launcherTemplates.size()); ++i) {
            const bool selected = i == form.templateIndex;
            if (ImGui::Selectable(g_launcherTemplates[static_cast<std::size_t>(i)].c_str(), selected))
                form.templateIndex = i;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##name", "my-game", form.name, sizeof(form.name));

    ImGui::Spacing();
    ImGui::TextUnformatted("Location");
    const float browseWidth = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 4.0f;
    ImGui::SetNextItemWidth(-(browseWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##parent", form.parent, sizeof(form.parent));
    ImGui::SameLine();
    ImGui::BeginDisabled(!view.canBrowse);
    if (ImGui::Button("Browse...", ImVec2(browseWidth, 0.0f)))
        view.browse = true;
    ImGui::EndDisabled();
    if (!view.canBrowse && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("This build has no folder picker. Type a path instead.");

    ImGui::Spacing();

    // The refusal is shown BEFORE the button is pressed, which is the difference
    // between a form and a form that argues with you. The line is reserved
    // whether or not it is showing, so the Create button does not jump out from
    // under the pointer the moment somebody types a space.
    const bool nameOk = validProjectName(form.name);
    if (form.name[0] != '\0' && !nameOk)
        ImGui::TextColored(themeColor(palette().warning), "Letters, digits, dashes and underscores.");
    else
        ImGui::NewLine();

    ImGui::BeginDisabled(!nameOk);
    if (primaryButton("Create project", ImVec2(-1.0f, ImGui::GetFrameHeight() * 1.4f), nameOk)) {
        const NewProjectResult result =
            createProject(view.templatesDir, view.definitions,
                          {.parent = std::filesystem::path(form.parent),
                           .name = form.name,
                           .templateName = g_launcherTemplates[static_cast<std::size_t>(form.templateIndex)]});
        if (result.error.has_value()) {
            view.message = core::engineCatalog().format(result.error->key, {});
            if (!result.error->detail.empty())
                view.message += " (" + result.error->detail + ")";
        }
        else {
            // Made and opened in one press, which is what somebody pressing
            // Create is asking for.
            view.open = result.path;
        }
    }
    ImGui::EndDisabled();
}

void drawLauncherOpen(LauncherView& view)
{
    LauncherForm& form = g_launcherForm;

    const float buttonWidth = ImGui::CalcTextSize("Open").x + ImGui::GetStyle().FramePadding.x * 4.0f;
    ImGui::SetNextItemWidth(-(buttonWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##openpath", "path to a project", form.open, sizeof(form.open));
    ImGui::SameLine();
    if (ImGui::Button("Open", ImVec2(buttonWidth, 0.0f))) {
        const std::filesystem::path chosen(form.open);
        if (chosen.empty())
            view.message = "Type a path, or press Browse beside the location field.";
        else if (!isProjectDirectory(chosen))
            view.message = chosen.string() + " is not a project: it has no luaug.toml and no src/scripts.";
        else
            view.open = chosen;
    }
    ImGui::BeginDisabled(!view.canBrowse);
    if (ImGui::Button("Browse for a project...", ImVec2(-1.0f, 0.0f)))
        view.browse = true;
    ImGui::EndDisabled();
}

// The band across the top: the wordmark, and which engine this is.
//
// A band rather than a line of text, because it is the only thing on this screen
// that is not a control and it should not read as one. `WindowPadding` is zero
// for this window so the band can reach both edges; the body child below puts
// the padding back.
void drawLauncherHeader()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ThemePalette& p = palette();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float pad = style.WindowPadding.x;
    const float height = ImGui::GetFrameHeight() * 2.2f;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + viewport->WorkSize.x, min.y + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(themeColor(p.surfaceRaised)));
    // The one accent rule on the screen, and it is under the name rather than
    // around a button: it says which product this is, which is the only thing
    // the header is for.
    draw->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), ImGui::ColorConvertFloat4ToU32(themeColor(p.accent)),
                  2.0f);

    // **`FontSizeBase`, not `GetFontSize()`.** The second is the size AFTER the
    // global scale factors, so feeding it back into `PushFont` multiplies the
    // scale in a second time -- ImGui's own header says so in capitals. Nothing
    // saw it because the scale was one until this milestone gave it a setting.
    ImGui::PushFont(nullptr, style.FontSizeBase * 1.75f);
    const float titleHeight = ImGui::GetFontSize();
    ImGui::SetCursorScreenPos(ImVec2(min.x + pad, min.y + (height - titleHeight) * 0.5f));
    ImGui::TextUnformatted("LuauG");
    const float titleWidth = ImGui::GetItemRectSize().x;
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(
        ImVec2(min.x + pad + titleWidth + style.ItemSpacing.x * 1.5f, min.y + (height - ImGui::GetFontSize()) * 0.5f));
    ImGui::TextDisabled("%s", LUAUG_VERSION_STRING);

    ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
}

// The last thing that happened, in a band of its own at the bottom.
//
// At the bottom rather than under whichever column produced it: a message that
// appears somewhere different depending on what you pressed is a message
// somebody has to hunt for, and half of these are refusals.
void drawLauncherFooter(const LauncherView& view, float height)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + ImGui::GetMainViewport()->WorkSize.x, min.y + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(themeColor(palette().surface)));
    draw->AddLine(min, ImVec2(max.x, min.y), ImGui::ColorConvertFloat4ToU32(themeColor(palette().border)));

    ImGui::SetCursorScreenPos(ImVec2(min.x + style.WindowPadding.x, min.y + (height - ImGui::GetFontSize()) * 0.5f));
    ImGui::TextColored(themeColor(palette().warning), "%s", view.message.c_str());
}

void drawLauncher(LauncherView* view)
{
    if (view == nullptr || view->projects == nullptr)
        return;

    LauncherForm& form = g_launcherForm;
    if (!form.seeded) {
        form.seeded = true;
        copyInto(form.parent, sizeof(form.parent), view->defaultParent.string());
        g_launcherTemplates = availableTemplates(view->templatesDir);
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    // Zero, so the header and the footer can be bands that reach both edges. The
    // body child puts the real padding back, which is the only way to have both
    // in one window.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar();

    drawLauncherHeader();

    const float footerHeight = view->message.empty() ? 0.0f : ImGui::GetFrameHeight() * 1.6f;
    if (ImGui::BeginChild("##body", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_AlwaysUseWindowPadding)) {
        // Two columns: what you have on the left, what you can make on the
        // right. The proportion is deliberate -- the list is the thing somebody
        // came for, and on every launch after the first it is the only thing
        // they touch.
        const float rightWidth = std::min(viewport->WorkSize.x * 0.38f, 420.0f);
        if (ImGui::BeginTable("##launcher-columns", 2, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("##left", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthFixed, rightWidth);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            sectionLabel("YOUR PROJECTS");
            drawLauncherProjects(*view);

            ImGui::TableSetColumnIndex(1);
            // **Making one comes before opening one**, which is the opposite of
            // the order this screen shipped with. Somebody who has projects
            // opens them from the list on the left; the right-hand column is
            // where somebody who has none goes, and for them the first thing
            // should be the thing they need.
            sectionLabel("NEW PROJECT");
            drawLauncherNew(*view);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            sectionLabel("OPEN AN EXISTING FOLDER");
            drawLauncherOpen(*view);

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    if (footerHeight > 0.0f)
        drawLauncherFooter(*view, footerHeight);

    ImGui::End();
}

void drawShell(const Frame& frame, scene::World* world, core::InstanceId root, Inspector* inspector,
               script::ScriptRuntime* runtime, const StreamingHost* streaming)
{
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("LuauG")) {
        drawStats(frame);

        // A host with no world is a normal state -- `--version`, the render
        // gates, a test with no scene -- and it gets the stats panel it has
        // always had.
        if (world != nullptr && inspector != nullptr) {
            ImGui::SeparatorText("Explorer");
            if (ImGui::BeginChild("explorer", ImVec2(0.0f, 200.0f), ImGuiChildFlags_Borders))
                // Everything, including what streaming made: this is the debug
                // overlay rather than the editor, and it exists to show the
                // world as it IS rather than as it was authored.
                drawExplorer(*world, root, *inspector, nullptr, nullptr, nullptr, true);
            ImGui::EndChild();

            ImGui::SeparatorText("Properties");
            drawProperties(*world, *inspector);
            drawWriteLog(*world, *inspector);
        }

        if (runtime != nullptr) {
            ImGui::SeparatorText("Memory");
            drawMemory(*runtime);
        }

        // Only for a project that streams. A panel of zeroes on every example
        // that does not would be furniture nobody can act on.
        if (streaming != nullptr && streaming->active()) {
            ImGui::SeparatorText("Streaming");
            drawStreaming(*streaming);
        }

        // The console draws even with no VM: the LOG half is the half a person
        // wants when the VM failed to boot, which is the moment they most want
        // it. The input line simply does nothing.
        ImGui::SeparatorText("Console");
        drawConsole(runtime);
    }
    ImGui::End();
}

} // namespace

DebugOverlay::DebugOverlay(platform::Window& window, rhi::IDevice& device, Shell shell, std::string layoutPath)
    : shell_(shell), layoutPath_(std::move(layoutPath))
{
    // The editor IS the application, so it is up from the first frame. F3 still
    // works and still hides it, which is the cheapest way to look at the world
    // without the furniture.
    visible_ = shell_ == Shell::Editor;

    SDL_Window* sdlWindow = platform::nativeWindow(window);
    SDL_GPUDevice* gpuDevice = rhi::nativeDevice(device);

    // Not a failure and not worth a message: `--rhi=capture` and `--rhi=null`
    // have nothing to draw with, and answering false from active() is the
    // entire contract for that case.
    if (sdlWindow == nullptr || gpuDevice == nullptr)
        return;

    if (ImGui::GetCurrentContext() != nullptr) {
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.already_running"));
        return;
    }

    // The pipeline ImGui builds is compiled against one colour format, and the
    // only source of the right one is the device-window pair. An unclaimed
    // window answers INVALID here rather than at the first draw, so the
    // ordering requirement is checked where it can still be explained.
    const SDL_GPUTextureFormat colorFormat = SDL_GetGPUSwapchainTextureFormat(gpuDevice, sdlWindow);
    if (colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.window_not_claimed"));
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    // Docking is why ADR 0011 pins the docking tag rather than the release one.
    // No dockspace host window is created: one panel does not need one, and the
    // editor that would is not in v1 (R15).
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Otherwise ImGui writes imgui.ini into the working directory, which under
    // CTest is the source tree (R14) and for a game is wherever it happened to
    // be launched from. Remembered window positions are not state a GAME has
    // decided to keep -- but they are exactly what an editor owes somebody who
    // arranged their panels once, so `Shell::Editor` names a file inside the
    // project it opened.
    io.IniFilename = nullptr;
    if (shell_ == Shell::Editor && !layoutPath_.empty()) {
        // ImGui writes the file and never the directory above it, and a project
        // that has not been built yet has no `.luaug/`. Failing here would mean
        // a layout that silently never persists, which is worse than a layout
        // that never existed.
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(layoutPath_).parent_path(), ec);
        io.IniFilename = layoutPath_.c_str();
    }
    // **The shell's own look (ADR 0056), before anything draws.** Two things
    // rather than one: `StyleColorsDark` was written for a debug window over a
    // game, and ImGui's default face is a 13 px bitmap designed for a debugger
    // -- which between them are most of the reason this editor read as an
    // overlay with panels rather than as an application.
    g_appearance = loadAppearance(appearanceFile());
    // Read once, here, because it is the display the window OPENED on. Following
    // a monitor change would mean re-rasterising the atlas mid-frame, and a
    // person who drags the editor to a second screen can reopen it -- which is
    // what every editor in this shape does today.
    g_displayScale = platform::windowDisplayScale(window);
    applyTheme(themeById(g_appearance.themeId), resolveUiScale(g_appearance.scale, g_displayScale));

    // Before the backend starts, so the atlas it builds is the one with Inter
    // in it. False is a normal outcome -- a build tree whose content has not
    // been staged -- and it is said once rather than drawn silently wrong.
    if (!loadUiFont())
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.font_missing"));
    // The code face, added second so the UI one stays `Fonts[0]` and therefore
    // the default. False is the same normal state and gets the same treatment:
    // the script editor falls back to the UI face, which is readable and wrongly
    // spaced, and the log says which.
    if (!loadCodeFont())
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.code_font_missing"));

    if (!ImGui_ImplSDL3_InitForSDLGPU(sdlWindow)) {
        ImGui::DestroyContext();
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.init_failed"));
        return;
    }

    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = gpuDevice;
    info.ColorTargetFormat = colorFormat;
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.init_failed"));
        return;
    }

    g_window = &window;
    g_device = &device;
    active_ = true;
}

DebugOverlay::~DebugOverlay()
{
    if (!active_)
        return;

    // **Written on the way out.** ImGui saves the ini by itself, but on a timer
    // -- so an arrangement made in the last few seconds before somebody quits is
    // an arrangement they made twice. `IniFilename` is null for every shell but
    // the editor, and `SaveIniSettingsToDisk` is only called when it is not.
    if (const ImGuiIO& io = ImGui::GetIO(); io.IniFilename != nullptr)
        ImGui::SaveIniSettingsToDisk(io.IniFilename);

    // Renderer first: it releases GPU objects through the device, which is
    // still alive because the constructor's contract says it must be.
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    g_window = nullptr;
    g_device = nullptr;
}

void DebugOverlay::handleEvents(std::span<const platform::Event> events)
{
    if (!active_)
        return;

    // ImGui models far more input than the engine does -- text, mouse capture,
    // window focus -- so it reads the untranslated stream. That stream existing
    // at all is what sdl_interop.h is for.
    // **While the pointer is held, the UI does not see the mouse.** Relative
    // mode hides the cursor and stops moving it, and SDL goes on posting motion
    // with a logical position it accumulates from the deltas -- so without this
    // the invisible cursor walks across the panels, highlighting rows in the
    // explorer and hovering buttons nobody is pointing at.
    //
    // **Two holders, and they need different things.** The EDITOR holds it
    // while a right-drag turns the fly camera (D063), and there only motion is
    // withheld: the button going UP still has to arrive, or ImGui believes it
    // is held forever and the turn never ends. The GAME holds it for as long as
    // somebody is playing (D069), and there the mouse is not the UI's at all --
    // motion, wheel and button PRESSES are all withheld, because a player
    // turning their head must not be clicking the explorer at the same time.
    //
    // A release is delivered in both cases and for the same reason: ImGui can
    // never be left believing a button it was never told about is down.
    const bool looking = editor_ != nullptr && editor_->lookInput().active;
    for (const SDL_Event& raw : platform::rawEvents()) {
        const bool motion = raw.type == SDL_EVENT_MOUSE_MOTION;
        if (looking && motion)
            continue;
        if (gameHoldsPointer_ &&
            (motion || raw.type == SDL_EVENT_MOUSE_WHEEL || raw.type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
            continue;
        }
        ImGui_ImplSDL3_ProcessEvent(&raw);
    }

    // **And the cursor is nowhere**, which withholding motion alone does not
    // say: ImGui would keep the last position it was told about, so whichever
    // row the cursor happened to be over when play was pressed would stay lit
    // for the whole session. `-FLT_MAX` is ImGui's own spelling of "there is no
    // mouse". The SDL3 backend will not overwrite it while relative mode is on
    // -- `ImGui_ImplSDL3_UpdateMouseData` guards its global-state fallback with
    // exactly that -- so this holds for the frame.
    if (gameHoldsPointer_)
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    for (const platform::Event& event : events) {
        // Repeats excluded: holding F3 down should not strobe the panel.
        if (event.type == platform::EventType::KeyDown && event.key == platform::Key::F3 && !event.repeat)
            visible_ = !visible_;
    }
}

void DebugOverlay::render(rhi::ICmdList& cmd, rhi::TextureHandle target, const Frame& frame)
{
    // **The editor draws while hidden, and every other shell does not.** In the
    // editor the world lives in a texture that only ImGui puts on the screen,
    // so an early return here is a black window; the shell below draws the
    // viewport alone in that state. The F3 overlay is drawn OVER a finished
    // frame and has nothing to show when it is down.
    if (!active_ || !target.valid())
        return;
    if (!visible_ && shell_ != Shell::Editor && shell_ != Shell::Launcher)
        return;

    // The frame currently being recorded. Null means the caller is outside
    // beginFrame()/submitAndPresent(), where there is nothing to draw into.
    SDL_GPUCommandBuffer* buffer = rhi::nativeCommandBuffer(*g_device);
    if (buffer == nullptr)
        return;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (shell_ == Shell::Launcher)
        drawLauncher(launcher_);
    else if (shell_ == Shell::Editor)
        drawEditorShell(frame, world_, root_, inspector_, runtime_, editor_, viewportTexture_, layoutBuilt_, commands_,
                        panels_, dialogs_, icons_, visible_);
    else
        drawShell(frame, world_, root_, inspector_, runtime_, streaming_);
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr)
        return;

    // Mandatory, and mandatory HERE: this is where the vertex and index buffers
    // are uploaded, and a copy cannot run inside a render pass. The backend
    // header says so in capitals for the same reason our seam does.
    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, buffer);

    // Load, not Clear: the overlay is drawn on top of a finished frame.
    const std::array<rhi::ColorAttachment, 1> colors{rhi::ColorAttachment{
        .texture = target,
        .loadOp = rhi::LoadOp::Load,
        .storeOp = rhi::StoreOp::Store,
    }};

    cmd.pushDebugGroup("debug-overlay");
    cmd.beginRenderPass({.colorAttachments = colors, .debugName = "imgui"});

    // Opened through the seam a line ago, so this is the pass just begun -- the
    // device owns exactly one command list, which is the one `cmd` refers to.
    if (SDL_GPURenderPass* pass = rhi::nativeRenderPass(*g_device); pass != nullptr)
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, buffer, pass);

    cmd.endRenderPass();
    cmd.popDebugGroup();
}

void DebugOverlay::captureLog()
{
    ConsoleLog& log = console();
    if (log.installed)
        return;
    log.installed = true;

    log.previous = core::setLogSink([](core::LogLevel level, std::string_view text) {
        ConsoleLog& sink = console();
        {
            std::lock_guard<std::mutex> lock(sink.mutex);
            sink.lines.push_back(ConsoleLog::Line{level, std::string(text)});
            while (sink.lines.size() > ConsoleLog::kMaxLines)
                sink.lines.pop_front();
        }
        // Chained rather than replaced: the console pane and the log FILE both
        // get every line. A shell that ate the log would be the last place
        // anybody looked for it.
        if (sink.previous)
            sink.previous(level, text);
    });
}

#else

// ADR 0011: a shipping build contains no ImGui, so the overlay contains no
// behaviour. The class keeps its shape and its signatures -- that is what lets
// the frame loop call it without an #ifdef -- and active() answers false, which
// is how anything that asks finds out there is nothing here.

DebugOverlay::DebugOverlay(platform::Window&, rhi::IDevice&, Shell, std::string)
{}

DebugOverlay::~DebugOverlay() = default;

void DebugOverlay::handleEvents(std::span<const platform::Event>)
{}

void DebugOverlay::render(rhi::ICmdList&, rhi::TextureHandle, const Frame&)
{}

// Nothing to capture INTO: the ring buffer and the console pane that reads it
// live in the half of this file that ImGui compiles. Leaving the process log
// sink alone is the whole behaviour -- the log FILE keeps every line, which is
// where a shipping build's log was always going to be read from.
void DebugOverlay::captureLog()
{}

#endif

} // namespace luaug::app
