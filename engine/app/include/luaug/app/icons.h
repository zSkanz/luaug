#pragma once

#include "luaug/core/types.h"
#include "luaug/rhi/types.h"

#include <filesystem>
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
    // actually draws: a tree row, a browser row and a toolbar button.
    static constexpr u32 Sizes[] = {16u, 24u, 32u};

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
    struct Source
    {
        std::filesystem::path root;
        // Empty for a project's loose-file override directory, which has no
        // manifest by design -- one file replaces one icon.
        std::unordered_map<std::string, std::string> icons;
        bool loose = false;
    };

    [[nodiscard]] static bool readTheme(const std::filesystem::path& themeDir, Source& out, std::string& fallbackId);

    std::vector<Source> m_sources;
    std::unordered_map<std::string, Cell> m_cells;
    std::string m_fallbackId;
    std::string m_status;
    rhi::TextureHandle m_texture;
    u32 m_atlasSize = 0;
};

} // namespace luaug::app
