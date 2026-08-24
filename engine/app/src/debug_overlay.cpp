#include "luaug/app/debug_overlay.h"

#if LUAUG_DEBUG_UI

#include "luaug/app/backends.h"
#include "luaug/app/icons.h"
#include "luaug/core/log.h"
#include "luaug/core/math.h"
#include "luaug/core/text_key.h"
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
// `frameless` drops the button's background and its padding, leaving the
// picture and its hit box.
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
                const char* tip, bool frameless = false)
{
    if (frameless) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    }
    const auto unstyle = [frameless]() {
        if (frameless) {
            ImGui::PopStyleVar();
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
[[nodiscard]] std::string classIconId(const scene::World& world, core::InstanceId id)
{
    const scene::ClassDescriptor* descriptor = world.classes().find(world.classOf(id));
    if (descriptor == nullptr)
        return std::string(icons::ClassInstance);
    return "class." + std::string(world.atoms().text(descriptor->name));
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
std::vector<TreeRow> g_rows;
// The rows a person could actually see if they scrolled: `g_rows` minus every
// subtree under a closed ancestor. Computed before anything is drawn, because a
// clipper needs to know how many rows exist before it decides which to draw.
std::vector<TreeRow> g_visible;
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
                  EditorDialogs* dialogs, const IconAtlas* icons, bool showGenerated, bool drawRoot = false)
{
    // How much of the depth is above the first row that gets drawn. One when
    // the root is hidden, zero when it is not -- and every position on a row is
    // measured from it.
    const u32 depthBase = drawRoot ? 0u : 1u;

    collectTree(world, root, g_rows);

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

    // Preorder, so an ancestor is always decided before its descendants. A row
    // is visible when every ancestor above it is open; `hiddenBelow` remembers
    // the shallowest closed depth rather than re-walking upwards per row.
    g_visible.clear();
    u32 hiddenBelow = std::numeric_limits<u32>::max();
    u32 generatedBelow = std::numeric_limits<u32>::max();
    for (const TreeRow& row : g_rows) {
        // **What streaming made is not the scene.** It pumps in edit mode as
        // well as in play -- deliberately, because a world you cannot see is a
        // world you cannot edit -- but the serializer skips a generated subtree
        // whole and nothing authored may live in one, so sixty `Chunk_x_y_z`
        // folders standing between a person and the four things they wrote is
        // the root's own complaint again: scrolling past a world to find the
        // thing you came for. Window > Streamed Content brings them back.
        //
        // A depth watermark rather than an ancestry walk per row, exactly like
        // `hiddenBelow` below it: the tree is preorder, so a generated folder's
        // whole subtree is the run of rows deeper than it.
        if (row.depth > generatedBelow)
            continue;
        generatedBelow = std::numeric_limits<u32>::max();
        if (!showGenerated && world.generated(row.id)) {
            generatedBelow = row.depth;
            continue;
        }

        if (row.depth > hiddenBelow)
            continue;
        hiddenBelow = std::numeric_limits<u32>::max();

        // **The root is not drawn.** `game` has no properties worth a row, it
        // cannot be renamed, deleted, duplicated or reparented, and every
        // useful thing in a world is under it -- so a row for it is a line of
        // chrome and an indent level charged to every row beneath it. The
        // services are the top of this tree, which is what a person opening the
        // panel is looking for.
        //
        // Filtered here rather than by walking from somewhere else, because
        // `root` is what `isEngineOwned` compares against and what the pick
        // path resolves into: the tree is still the tree, this is the view of
        // it. Depth is shifted at the draw so the services sit flush left.
        if (row.depth > 0 || drawRoot)
            g_visible.push_back(row);

        const bool hasChildren = world.childCount(row.id) > 0;
        // The services under `game` are what anyone opening this wants to see;
        // deeper than that is a project's own tree and is its business. Seeded
        // once per instance rather than every frame, so collapsing one stays
        // collapsed.
        if (hasChildren && !g_openKnown.contains(row.id.index)) {
            g_openKnown.insert(row.id.index);
            // **The root only**, and when it is the world's it is not drawn --
            // opening it is what puts the services on screen at all. What is
            // INSIDE them is the scene, and showing all of that means scrolling
            // past a world to find the thing you came for.
            //
            // A stamp's root opens for the opposite reason: it IS what somebody
            // opened, and a stage that starts collapsed shows one row.
            if (row.depth == 0)
                g_open.insert(row.id.index);
        }
        if (hasChildren && !g_open.contains(row.id.index))
            hiddenBelow = row.depth;
    }

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
                if (drawIcon(icons, classIconId(world, row.id), iconSize, Editor::folderColor(world, row.id))) {
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%.*s", static_cast<int>(className.size()), className.data());
                    penX += iconSize + ImGui::GetStyle().ItemInnerSpacing.x;
                }
            }

            ImGui::SetCursorPos(ImVec2(penX, centred(ImGui::GetTextLineHeight())));
            ImGui::TextUnformatted(label);
            penX += ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x;

            // **A stamped row says so.** Changing the file changes this
            // instance, which is the whole point of the mark and exactly the
            // kind of thing somebody should not have to remember.
            if (const core::NameAtom stamp = world.stampOf(row.id); stamp.valid()) {
                ImGui::SetCursorPos(ImVec2(penX, centred(ImGui::GetTextLineHeight())));
                ImGui::TextDisabled("(stamp)");
                if (ImGui::IsItemHovered()) {
                    const std::string_view text = world.atoms().text(stamp);
                    ImGui::SetTooltip("follows content/%.*s -- editing anything inside breaks the link",
                                      static_cast<int>(text.size()), text.data());
                }
                penX += ImGui::CalcTextSize("(stamp)").x + ImGui::GetStyle().ItemSpacing.x;
            }

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
                ImGui::InputTextWithHint("##add-filter", "filter", g_addFilter.data(), g_addFilter.size());

                const std::string_view filter(g_addFilter.data());
                if (ImGui::BeginChild("add-list", ImVec2(210.0f, 260.0f))) {
                    for (const scene::ClassId classId : g_creatable) {
                        const scene::ClassDescriptor* candidate = world.classes().find(classId);
                        if (candidate == nullptr)
                            continue;
                        const std::string_view candidateName = world.atoms().text(candidate->name);
                        if (!filter.empty() && !containsFold(candidateName, filter))
                            continue;

                        char item[96];
                        (void)std::snprintf(item, sizeof(item), "%.*s", static_cast<int>(candidateName.size()),
                                            candidateName.data());
                        if (ImGui::Selectable(item)) {
                            commands->createClass = classId;
                            commands->createParent = row.id;
                            ImGui::CloseCurrentPopup();
                        }
                        // The IDL's own prose, which the properties grid already
                        // shows for a property and which is the only description
                        // of a class anywhere at runtime.
                        if (candidate->doc[0] != 0 && ImGui::IsItemHovered()) {
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

void drawEditor(scene::World& world, Inspector& inspector, std::span<const core::InstanceId> targets,
                const scene::PropertyDesc& descriptor, const SharedValue& shared)
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

    const EditorKind kind = editorFor(descriptor.type);
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

void drawProperties(scene::World& world, Inspector& inspector)
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
            const EditorKind kind = editorFor(descriptor->type);

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
            drawEditor(world, inspector, targets, *descriptor, shared);

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

void drawConsole(script::ScriptRuntime* runtime)
{
    ConsoleLog& log = console();

    if (ImGui::BeginChild("log", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Borders)) {
        std::lock_guard<std::mutex> lock(log.mutex);
        for (const ConsoleLog::Line& line : log.lines) {
            const ImVec4 colour = line.level == core::LogLevel::Error   ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                  : line.level == core::LogLevel::Warn  ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                                  : line.level == core::LogLevel::Debug ? ImVec4(0.6f, 0.65f, 0.75f, 1.0f)
                                                                        : ImVec4(0.85f, 0.88f, 0.92f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
        }
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
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.72f, 0.35f, 1.0f));
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

    ImGui::SameLine();
    if (ImGui::Button(editor.gizmoLocal() ? "local" : "world"))
        editor.setGizmoLocal(!editor.gizmoLocal());
    ImGui::SetItemTooltip(editor.gizmoLocal() ? "the selection's own axes -- click for the world's"
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
        const ImVec4 colour =
            editor.status().failed ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f) : ImVec4(0.55f, 0.75f, 0.55f, 1.0f);
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
void drawViewport(Editor& editor, rhi::TextureHandle texture, EditorCommands& commands, bool& open,
                  const IconAtlas* icons)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("viewport", &open);
    ImGui::PopStyleVar();

    if (visible) {
        drawTransport(editor, commands, icons);

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
                if (const ImGuiPayload* dropped = ImGui::AcceptDragDropPayload(kContentDragPayload);
                    dropped != nullptr) {
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

    ImGui::DockBuilderDockWindow("viewport", centre);
    ImGui::DockBuilderDockWindow("explorer", left);
    ImGui::DockBuilderDockWindow("properties", right);
    ImGui::DockBuilderDockWindow("stats", right);
    // Content first, so it is the tab that opens. The two share a node on
    // purpose -- they are both "the thing under the viewport" and neither
    // deserves permanent floor space -- but which one greets somebody is a
    // decision rather than a consequence of call order, so it is also set
    // explicitly below.
    ImGui::DockBuilderDockWindow("content", bottom);
    ImGui::DockBuilderDockWindow("console", bottom);

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
        // **The set owes a `content.Stamp` drawing** and this is the honest
        // stand-in until it exists: a `Model` is a group of instances handled
        // as one thing, which is what a stamp is a file of. Falling back to the
        // generic content icon would say less than that.
        return icons::ClassModel;
    case ContentKind::Mesh:
        return icons::ContentMesh;
    case ContentKind::Texture:
        return icons::ContentTexture;
    case ContentKind::Chunk:
        return icons::ContentChunk;
    case ContentKind::Other:
        break;
    }
    return icons::ContentOther;
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
// `drawExplorer` beside this one does NOT do that yet, which is a thing to fix
// rather than a precedent to follow.
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

void drawContent(Editor& editor, EditorCommands& commands, EditorPanels& panels, EditorDialogs& dialogs,
                 const IconAtlas* icons)
{
    if (!ImGui::Begin("content", &panels.content)) {
        ImGui::End();
        return;
    }

    ContentTree& tree = editor.content();
    const float toolbarIcon = ImGui::GetTextLineHeight();

    ImGui::BeginDisabled(tree.atRoot());
    if (iconButton(icons, icons::ActionUp, toolbarIcon, "up", "up", "up one folder", true))
        (void)tree.leave();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (iconButton(icons, icons::ActionRefresh, toolbarIcon, "refresh", "refresh", "re-read this folder", true))
        (void)tree.refresh();

    ImGui::SameLine();
    // The shell's dialog, so the toolbar button and the folder's own
    // right-click menu reach the same one. A FOLDER rather than a generic
    // "new", because what it makes is a folder and the picture can say so.
    if (iconButton(icons, icons::ContentFolder, toolbarIcon, "new-folder", "new folder", "new folder", true))
        dialogs.newFolder = true;

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
    (void)drawIcon(icons, icons::ContentFolder, toolbarIcon);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (tree.atRoot()) {
        // Where you already are is a label, not a control: the step that goes
        // nowhere is drawn the same way at the end of the chain.
        ImGui::TextUnformatted("content");
    }
    else if (ImGui::SmallButton("content")) {
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
            (void)drawIcon(icons, icons::ContentFolder, toolbarIcon, editor.contentColor(here));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            // The one you are IN is not a button: it goes nowhere, and a
            // control that does nothing is worse than a label.
            if (last) {
                ImGui::TextUnformatted(segment.c_str());
            }
            else if (ImGui::SmallButton(segment.c_str())) {
                for (int up = 0; up < depth - step - 1; ++up)
                    (void)tree.leave();
            }
            ImGui::PopID();
        }
    }

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
                           "how entries are laid out", true)) {
                ImGui::OpenPopup("view-menu");
            }
        }
        if (ImGui::BeginPopup("view-menu")) {
            for (int index = 0; index < 3; ++index) {
                const auto view = static_cast<EditorPanels::ContentView>(index);
                if (ImGui::MenuItem(names[index], nullptr, panels.contentView == view))
                    panels.contentView = view;
            }
            ImGui::EndPopup();
        }
    }

    ImGui::Separator();

    if (ImGui::BeginChild("entries")) {
        const std::vector<ContentEntry>& entries = tree.entries();
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

        ImGuiListClipper clipper;
        clipper.Begin(rows, pitch);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const int index = row * columns + column;
                    if (index >= static_cast<int>(entries.size()))
                        break;
                    const ContentEntry& entry = entries[static_cast<std::size_t>(index)];
                    ImGui::PushID(index);

                    const ImVec2 entryOrigin(gridOrigin.x + static_cast<float>(column) * strideX,
                                             gridOrigin.y + static_cast<float>(row) * pitch);
                    ImGui::SetCursorPos(entryOrigin);
                    const float entryIcon = layout.icon;

                    const bool isOpenScene = entry.kind == ContentKind::Scene && entry.path == editor.openScenePath();
                    if (isOpenScene)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 0.55f, 1.0f));

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
                            commands.openScene = entry.path;
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
                        ImGui::TextUnformatted(entry.name.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginPopupContextItem("entry-menu")) {
                        // The rows are drawn with no vertical spacing; a menu is
                        // not a row.
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, entrySpacing);
                        if (entry.kind == ContentKind::Scene && ImGui::MenuItem("Open"))
                            commands.openScene = entry.path;
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
                    if (layout.nameBelow) {
                        const float centreX = entryOrigin.x + (layout.cell.x - entryIcon) * 0.5f;
                        ImGui::SetCursorPos(ImVec2(centreX, entryOrigin.y + ImGui::GetStyle().ItemInnerSpacing.y));
                        (void)drawIcon(icons, contentKindIcon(entry.kind), entryIcon, tint);

                        const std::string label = elideToWidth(entry.name, layout.cell.x);
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
                        if (drawIcon(icons, contentKindIcon(entry.kind), entryIcon, tint))
                            entryX += entryIcon + ImGui::GetStyle().ItemInnerSpacing.x;

                        const float textY = entryOrigin.y + (entryHeight - ImGui::GetTextLineHeight()) * 0.5f;
                        ImGui::SetCursorPos(ImVec2(entryX, textY));
                        ImGui::TextUnformatted(entry.name.c_str());
                        entryX += ImGui::CalcTextSize(entry.name.c_str()).x + 12.0f;

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
        if (ImGui::MenuItem("New Scene"))
            commands.newScene = true;
        ImGui::Separator();
        // Greyed rather than hidden when there is nothing to save to: the item
        // has to be where somebody expects it even when it cannot act, or they
        // conclude the editor cannot do it at all.
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !editor.openScenePath().empty()))
            commands.save = true;
        if (ImGui::MenuItem("Save Scene As..."))
            dialogs.saveAs = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            commands.quit = true;
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
                           "and lives in luaug.toml.");

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
                     EditorCommands& commands, EditorPanels& panels, EditorDialogs& dialogs, IconAtlas* icons)
{
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
        const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
        // Asked for, or never arranged. `DockBuilderRemoveNode` throws away an
        // arrangement somebody chose, so it only runs when they said so or when
        // there is nothing to throw away.
        if (asked || node == nullptr || (!node->IsSplitNode() && node->Windows.Size == 0)) {
            buildDefaultLayout(dockspace);
            builtThisFrame = true;
            if (asked)
                panels = EditorPanels{};
        }
    }

    if (editor != nullptr) {
        if (panels.viewport)
            drawViewport(*editor, viewport, commands, panels.viewport, icons);
        if (panels.content)
            drawContent(*editor, commands, panels, dialogs, icons);
    }

    if (panels.explorer) {
        if (ImGui::Begin("explorer", &panels.explorer)) {
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
                             editingStamp);
            }
        }
        ImGui::End();
    }

    if (panels.properties) {
        if (ImGui::Begin("properties", &panels.properties)) {
            if (world != nullptr && inspector != nullptr) {
                drawProperties(*world, *inspector);
                drawWriteLog(*world, *inspector);
            }
        }
        ImGui::End();
    }

    // Draws with no VM for the same reason it does in the overlay: the LOG half
    // is what somebody wants when the VM failed to boot.
    if (panels.console) {
        if (ImGui::Begin("console", &panels.console))
            drawConsole(runtime);
        ImGui::End();
    }

    if (panels.stats) {
        if (ImGui::Begin("stats", &panels.stats)) {
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
    if (editor != nullptr && world != nullptr && inspector != nullptr && !ImGui::IsAnyItemActive() && !popupOpen &&
        !editor->inPlayMode() && inspector->selectionCount() > 0) {
        const bool engineOwned = Editor::isEngineOwned(*world, inspector->selection(), root);

        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !engineOwned)
            commands.deleteSelection = true;
        if (ImGui::IsKeyPressed(ImGuiKey_D, false) && ImGui::GetIO().KeyCtrl && !engineOwned)
            commands.duplicateSelection = true;

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
    // was built: a person who later chose the console should find the console.
    if (builtThisFrame)
        ImGui::SetWindowFocus("content");
}

void drawShell(const Frame& frame, scene::World* world, core::InstanceId root, Inspector* inspector,
               script::ScriptRuntime* runtime)
{
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("LuauG")) {
        drawStats(frame);

        // A host with no world is a normal state -- `--version`, the render
        // gates, a test with no scene -- and it gets the stats panel it has
        // always had.
        if (world != nullptr && inspector != nullptr) {
            ImGui::SeparatorText("explorer");
            if (ImGui::BeginChild("explorer", ImVec2(0.0f, 200.0f), ImGuiChildFlags_Borders))
                // Everything, including what streaming made: this is the debug
                // overlay rather than the editor, and it exists to show the
                // world as it IS rather than as it was authored.
                drawExplorer(*world, root, *inspector, nullptr, nullptr, nullptr, true);
            ImGui::EndChild();

            ImGui::SeparatorText("properties");
            drawProperties(*world, *inspector);
            drawWriteLog(*world, *inspector);
        }

        if (runtime != nullptr) {
            ImGui::SeparatorText("memory");
            drawMemory(*runtime);
        }

        // The console draws even with no VM: the LOG half is the half a person
        // wants when the VM failed to boot, which is the moment they most want
        // it. The input line simply does nothing.
        ImGui::SeparatorText("console");
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
    ImGui::StyleColorsDark();

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
    if (!active_ || !visible_ || !target.valid())
        return;

    // The frame currently being recorded. Null means the caller is outside
    // beginFrame()/submitAndPresent(), where there is nothing to draw into.
    SDL_GPUCommandBuffer* buffer = rhi::nativeCommandBuffer(*g_device);
    if (buffer == nullptr)
        return;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (shell_ == Shell::Editor)
        drawEditorShell(frame, world_, root_, inspector_, runtime_, editor_, viewportTexture_, layoutBuilt_, commands_,
                        panels_, dialogs_, icons_);
    else
        drawShell(frame, world_, root_, inspector_, runtime_);
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
