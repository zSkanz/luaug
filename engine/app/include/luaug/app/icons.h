#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/rhi/types.h"
#include "luaug/scene/class_registry.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The editor's icons, resolved from a theme and packed into one atlas.
//
// **This is the only thing that knows about themes**, which is what
// `icons/README.md` asks for and what makes a plugin theme a case in a list
// rather than an edit at every call site. A caller asks for a logical id and a
// size and gets a texture and four texture coordinates; where the picture came
// from is this file's business.
//
// ## The ids
//
// Three namespaces, and the prefix says which question the icon answers:
//
//   `class.<ClassName>`   the Explorer's rows, and the lookup is mechanical --
//                         `"class." + the class's own name`
//   `content.<Kind>`      a `ContentKind` from `content_tree.h`
//   `action.<Name>`       something a person clicks
//
// `icon_ids.gen.h` carries them as constants generated from
// `icons/default/theme.json`, so a typo is a compile error and a missing icon
// is a build failure rather than a blank square. The class ids are still looked
// up by string, because the Explorer draws whatever class the world holds and a
// project may register one this build has never heard of -- that lookup misses
// and falls back, which is the designed behaviour and not a defect.
//
// ## What a theme is, and why a partial one is legal
//
// A directory with a `theme.json`. **Only `default` has to be complete**, and
// that rule is what makes overriding one icon cost one file: a plugin or a
// project ships the ids it cares about and inherits the rest. A miss walks the
// chain to `default` and then to `default`'s own `fallback` id.
//
// ## Why one atlas at a few fixed sizes
//
// The masters are 256 px and the panels draw at 16 to 32. Sampling a 256 px
// image down to 16 with the UI's bilinear filter takes two texels out of two
// hundred and fifty-six, which is not a small icon but a smudge -- so every
// size the editor asks for is box-filtered once, at load, into one atlas.
//
// One atlas rather than sixty-seven textures because the tree redraws every
// frame and ImGui batches by texture: seventy binds a frame for a panel is a
// profile artefact somebody eventually has to explain.
//
// ## Tint
//
// Every image is a MASK: white everywhere, with the meaning in the alpha. One
// file therefore serves a light panel and a dark one, multiplied by a colour at
// draw time. There is no second set for dark mode and there must not be -- two
// drawings of one icon drift the first time somebody touches one of them.
//
// **And the colour says what KIND of thing it is, never which thing.** The shape
// carries the identity; the tint only makes a long tree scannable. A theme
// declares a `palette` of a dozen ROLES and a sparse `roles` map from id to
// role, and an id nobody names takes `defaultRole` -- which is why a toolbar is
// one colour with four exceptions rather than a fruit salad.
//
// Two values per role, `light` and `dark`, and they are not two decisions: a
// single colour cannot clear 3:1 against both a near-white panel and a dark one,
// and everything in the band that does is muddy. They are solved from one hue so
// nobody picks the second by eye.
//
// **Tinting is switchable off**, and with it off every icon takes the panel's
// own foreground -- which is where this started. The set was drawn and
// collision-checked in a single ink before any colour existed, so the uncoloured
// editor is not a degraded one, and somebody who cannot use the colours must not
// get a worse tool.

namespace luaug::rhi {
class ICmdList;
class IDevice;
} // namespace luaug::rhi

namespace luaug::app {
using core::f32;
using core::u32;
using core::usize;

// Where an icon sits in the atlas. `valid` is false when the id resolved to
// nothing at all -- which can only happen when even the fallback is missing,
// i.e. when no theme loaded.
struct IconSprite
{
    f32 u0 = 0.0f;
    f32 v0 = 0.0f;
    f32 u1 = 0.0f;
    f32 v1 = 0.0f;
    bool valid = false;
};

class IconAtlas
{
public:
    IconAtlas() = default;
    ~IconAtlas() = default;

    IconAtlas(const IconAtlas&) = delete;
    IconAtlas& operator=(const IconAtlas&) = delete;

    // The sizes baked into the atlas, in pixels. Small enough a set that the
    // whole thing is one 1024-square texture, and chosen for what the editor
    // actually draws: a tree row, a browser row, a toolbar button -- and the
    // content browser's large-icon view, which is what 64 is for.
    //
    // **A size the atlas does not bake is a size the panel draws badly.** The
    // browser's icon view asks for roughly three times a frame's height, and
    // before 64 existed `find` handed back the 32 and ImGui scaled it up: a
    // smudge, in the one view whose entire purpose is that you can see what the
    // thing is.
    static constexpr u32 Sizes[] = {16u, 24u, 32u, 64u};

    // Reads the theme chain and builds the atlas. Idempotent: a second call
    // with the same roots does nothing.
    //
    // `contentDir` is `platform::paths().contentDir` -- the engine's own
    // content, where the build staged `icons/`. `projectDir` is the open
    // project or empty; a project's per-icon overrides live in
    // `<project>/.luaug/icons/<id>.png`.
    //
    // Needs a command list because uploading a texture is one, so this belongs
    // inside a frame rather than in a constructor.
    bool load(rhi::IDevice& device, rhi::ICmdList& cmd, const std::filesystem::path& contentDir,
              const std::filesystem::path& projectDir);

    void destroy(rhi::IDevice& device);

    [[nodiscard]] bool ready() const noexcept { return m_texture.valid(); }
    [[nodiscard]] rhi::TextureHandle texture() const noexcept { return m_texture; }

    // The sprite for `id` at the smallest baked size that is at least `size`.
    // A miss walks the theme chain and then the fallback id, so this returns
    // something for every id as long as a theme loaded.
    [[nodiscard]] IconSprite find(std::string_view id, u32 size) const;

    // Which panel an icon is being drawn on. Decided from the panel's own
    // background rather than configured, so it follows a style change for free
    // and there is no second setting to keep in sync.
    enum class Panel : core::u8
    {
        Light,
        Dark,
    };

    // The role colour for `id`, or nothing when the theme declares no palette or
    // tinting is off. A caller that gets nothing uses its own text colour, which
    // is what every caller did before there was a palette.
    //
    // An id absent from `roles` takes `defaultRole`; a role naming a palette key
    // no theme in the chain has takes `defaultRole` too, rather than drawing
    // nothing -- a typo in a plugin's manifest must not make an icon vanish.
    [[nodiscard]] std::optional<core::Color3> tintFor(std::string_view id, Panel panel) const;

    // --- The badge ------------------------------------------------------------
    //
    // **An overlay is not an icon in a row.** It is a mark in the CORNER of one,
    // and it is the only namespace whose ids are never drawn on their own: a
    // badge is two draws, a solid silhouette in the panel's own background
    // followed by the same silhouette with the mark cut out, in the foreground.
    //
    // **The knockout is what makes it exist, and the measurement says so.**
    // Across the class set at 16 px, 37 of 42 icons already have ink where the
    // badge goes -- 51% under `class.Workspace`, 49% under `class.Folder` -- so
    // a badge drawn without it lands on a folder's body and disappears.
    //
    // The numbers live in the THEME rather than here, which is the point: a
    // plugin can move the badge or resize it with no code at all. A theme with
    // no `overlay` block gets these, which are `default`'s own.
    struct Overlay
    {
        // Of the icon's edge.
        f32 scale = 0.40f;
        // The knockout is drawn this much bigger than the mark, so the hole has
        // a rim. The two share an outer silhouette exactly, so scaling them
        // apart is what this ratio is FOR -- doing it any other way puts the
        // rim on one side.
        f32 haloScale = 1.22f;
        // `bottom-right` is the only corner `default` uses; the field is here so
        // a theme can say otherwise.
        enum class Corner : core::u8
        {
            BottomRight,
            BottomLeft,
            TopRight,
            TopLeft,
        };
        Corner corner = Corner::BottomRight;
    };

    [[nodiscard]] const Overlay& overlay() const noexcept { return m_overlay; }

    [[nodiscard]] bool tinting() const noexcept { return m_tinting; }
    void setTinting(bool on) noexcept { m_tinting = on; }

    // Whether `id` resolved to a drawing of its own rather than to the
    // fallback. The Explorer uses it to tell "this class has an icon" from
    // "this class is wearing the generic one", which is the difference between
    // a set that looks finished and one that looks broken.
    [[nodiscard]] bool has(std::string_view id) const;

    // What the last load did, for the console. English literals: R3 does not
    // govern what the editor draws (ADR 0046).
    [[nodiscard]] const std::string& status() const noexcept { return m_status; }

private:
    struct Cell
    {
        // One per entry of `Sizes`, in the same order.
        IconSprite sprites[std::size(Sizes)];
    };

    // Resolution order, first hit wins, and the whole of what makes a plugin
    // theme work: a project override, then plugin themes in load order, then
    // the active theme, then `default`. Adding a source is adding to this list.
    struct RoleColor
    {
        core::Color3 light;
        core::Color3 dark;
    };

    struct Source
    {
        std::filesystem::path root;
        // Empty for a project's loose-file override directory, which has no
        // manifest by design -- one file replaces one icon.
        std::unordered_map<std::string, std::string> icons;
        // **Sparse overlays, exactly like `icons`.** A plugin that wants one
        // class in its own colour ships a palette key and an id pointing at it,
        // and no PNG at all -- which is the whole reason these are layered
        // rather than read from `default` alone.
        std::unordered_map<std::string, RoleColor> palette;
        std::unordered_map<std::string, std::string> roles;
        std::string defaultRole;
        // Layered like everything else: the first theme in the chain that
        // declares a block supplies it whole.
        std::optional<Overlay> overlay;
        bool loose = false;
    };

    [[nodiscard]] static bool readTheme(const std::filesystem::path& themeDir, Source& out, std::string& fallbackId);

    std::vector<Source> m_sources;
    std::unordered_map<std::string, Cell> m_cells;
    std::string m_fallbackId;
    Overlay m_overlay;
    bool m_tinting = true;
    std::string m_status;
    rhi::TextureHandle m_texture;
    u32 m_atlasSize = 0;
};

// The icon id for a class, falling back UP the class hierarchy before falling
// back to `Instance`.
//
// **A leaf class should not owe the art set a drawing.** `HingeConstraint`,
// `BallSocketConstraint` and `FixedConstraint` are three ways of saying
// "constraint" and the set has drawn none of them, so all three came out as the
// generic instance -- which is the icon for "I have no idea what this is", and
// the tree did have an idea. Walking up answers `Constraint` for all three the
// day one of them is drawn, and answers `Part` for a project's own class that
// extends one, which is what somebody who wrote `MyDoor extends Part` expects.
//
// It is the rule the content browser already follows from the other direction --
// a stamp wears the icon of the instance it is a file of -- said once, where
// both can reach it.
//
// Here rather than beside the panel that draws, because it is a MAPPING between
// two vocabularies and neither of them is ImGui: given a class, which drawing.
// A test can ask it without a window.
[[nodiscard]] std::string classIconFor(const IconAtlas* icons, const scene::ClassRegistry& classes,
                                       const core::AtomTable& atoms, scene::ClassId classId);

// The same, from a class NAME, which is what a stamp file carries: the browser
// reads the root class out of it and never sees an instance. A name this build
// has no class for answers `class.<name>`, so a theme may still draw it.
[[nodiscard]] std::string classIconFor(const IconAtlas* icons, const scene::ClassRegistry* classes,
                                       const core::AtomTable* atoms, std::string_view className);

} // namespace luaug::app
