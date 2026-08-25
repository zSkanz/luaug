#include "luaug/app/icons.h"

#include "luaug/asset/image.h"
#include "luaug/core/json.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"
#include "luaug/rhi/descs.h"
#include "luaug/rhi/device.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace luaug::app {
namespace {

using core::u8;

constexpr usize SizeCount = std::size(IconAtlas::Sizes);
// One padding texel around every cell. The ImGui SDL_GPU backend binds ONE
// sampler for the whole draw list and it is linear, so a cell flush against its
// neighbour bleeds that neighbour in along the shared edge at any size but the
// exact one.
constexpr u32 CellPadding = 1u;

// **A box filter, and it is the right one here.** Every source is a mask whose
// meaning is entirely in the alpha, downscaled by a whole factor of eight or
// more; a box average over the source footprint is what "how much of this
// pixel is covered" means, and a fancier kernel would ring against the hard
// edges these drawings are made of.
//
// RGB is written white rather than averaged. It already is white in every
// master, and forcing it removes the one way a resample can darken a tint: an
// averaged edge texel whose neighbours are transparent black drags the colour
// down while the alpha already says how much of it there is.
void boxDownscale(const asset::Image& source, u32 target, std::vector<u8>& out)
{
    out.assign(static_cast<usize>(target) * target * 4u, 0u);
    if (source.width == 0 || source.height == 0)
        return;

    const auto* pixels = reinterpret_cast<const u8*>(source.pixels.data());
    for (u32 y = 0; y < target; ++y) {
        const u32 y0 = y * source.height / target;
        const u32 y1 = std::max(y0 + 1u, (y + 1u) * source.height / target);
        for (u32 x = 0; x < target; ++x) {
            const u32 x0 = x * source.width / target;
            const u32 x1 = std::max(x0 + 1u, (x + 1u) * source.width / target);

            core::u32 alpha = 0;
            core::u32 count = 0;
            for (u32 sy = y0; sy < y1; ++sy) {
                for (u32 sx = x0; sx < x1; ++sx) {
                    alpha += pixels[(static_cast<usize>(sy) * source.width + sx) * 4u + 3u];
                    ++count;
                }
            }

            const usize index = (static_cast<usize>(y) * target + x) * 4u;
            out[index + 0] = 255u;
            out[index + 1] = 255u;
            out[index + 2] = 255u;
            out[index + 3] = count > 0 ? static_cast<u8>(alpha / count) : 0u;
        }
    }
}

void blit(std::vector<u8>& atlas, u32 atlasSize, const std::vector<u8>& cell, u32 cellSize, u32 originX, u32 originY)
{
    for (u32 y = 0; y < cellSize; ++y) {
        const usize dst = ((static_cast<usize>(originY) + y) * atlasSize + originX) * 4u;
        const usize src = static_cast<usize>(y) * cellSize * 4u;
        std::memcpy(atlas.data() + dst, cell.data() + src, static_cast<usize>(cellSize) * 4u);
    }
}

// `#RRGGBB`, and it is NOT linearised.
//
// The value is compared against ImGui's own style colours, which are in the same
// space it draws them in -- so converting to linear here would make a palette
// whose contrast was solved in sRGB come out wrong against the panel it was
// solved against. The floor in the shipped set is 3.88:1 and that number is only
// true in the space it was measured in.
[[nodiscard]] std::optional<core::Color3> parseHexColor(std::string_view text) noexcept
{
    if (text.size() != 7 || text[0] != '#')
        return std::nullopt;

    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    f32 channel[3]{};
    for (usize index = 0; index < 3; ++index) {
        const int high = nibble(text[1 + index * 2]);
        const int low = nibble(text[2 + index * 2]);
        if (high < 0 || low < 0)
            return std::nullopt;
        channel[index] = static_cast<f32>(high * 16 + low) / 255.0f;
    }
    return core::Color3{channel[0], channel[1], channel[2]};
}

} // namespace

bool IconAtlas::readTheme(const std::filesystem::path& themeDir, Source& out, std::string& fallbackId)
{
    std::string text;
    if (!platform::readTextFile(themeDir / "theme.json", text))
        return false;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(text, "theme.json"); !parsed.ok)
        return false;

    const core::JsonValue root = document.root();
    const core::JsonValue icons = root["icons"];
    for (usize i = 0; i < icons.size(); ++i) {
        const std::string_view key = icons.keyAt(i);
        const std::string_view relative = icons[key].asString();
        if (!key.empty() && !relative.empty())
            out.icons.emplace(std::string(key), std::string(relative));
    }

    // **The colour, and it is three sparse overlays rather than one block.** A
    // plugin that wants one class in its own colour ships a palette key and an
    // id pointing at it and no PNG at all, which is why each of these layers on
    // its own.
    const core::JsonValue palette = root["palette"];
    for (usize i = 0; i < palette.size(); ++i) {
        const std::string_view key = palette.keyAt(i);
        const core::JsonValue entry = palette[key];
        std::optional<core::Color3> light = parseHexColor(entry["light"].asString());
        std::optional<core::Color3> dark = parseHexColor(entry["dark"].asString());
        // Both or neither. A role with one value would be a role that is
        // unreadable on one of the two panels, which is the whole thing the
        // pair exists to prevent.
        if (!key.empty() && light.has_value() && dark.has_value())
            out.palette.emplace(std::string(key), RoleColor{*light, *dark});
    }

    const core::JsonValue roles = root["roles"];
    for (usize i = 0; i < roles.size(); ++i) {
        const std::string_view key = roles.keyAt(i);
        const std::string_view role = roles[key].asString();
        if (!key.empty() && !role.empty())
            out.roles.emplace(std::string(key), std::string(role));
    }

    if (const std::string_view declared = root["defaultRole"].asString(); !declared.empty())
        out.defaultRole = std::string(declared);

    // **The badge's geometry, in the theme rather than in the code.** That is
    // what lets a plugin move it or resize it without a line of C++, and it is
    // read whole rather than per-field: a theme that says `overlay` is
    // describing a badge, and half of one is not a thing to merge.
    if (const core::JsonValue block = root["overlay"]; block.type() == core::JsonType::Object) {
        IconAtlas::Overlay overlay;
        if (const core::f64 scale = block["scale"].asNumber(0.0); scale > 0.0)
            overlay.scale = static_cast<f32>(scale);
        if (const core::f64 halo = block["haloScale"].asNumber(0.0); halo > 0.0)
            overlay.haloScale = static_cast<f32>(halo);
        const std::string_view corner = block["corner"].asString();
        if (corner == "bottom-left")
            overlay.corner = IconAtlas::Overlay::Corner::BottomLeft;
        else if (corner == "top-right")
            overlay.corner = IconAtlas::Overlay::Corner::TopRight;
        else if (corner == "top-left")
            overlay.corner = IconAtlas::Overlay::Corner::TopLeft;
        else
            overlay.corner = IconAtlas::Overlay::Corner::BottomRight;
        out.overlay = overlay;
    }

    // The last theme consulted supplies the fallback, which for a chain that
    // always ends at `default` means `default`'s.
    if (const std::string_view declared = root["fallback"].asString(); !declared.empty())
        fallbackId = std::string(declared);

    out.root = themeDir;
    // **A theme with no `icons` at all is legal**, and it is the case the whole
    // overlay design exists for: a plugin recolouring somebody else's set ships
    // a palette and a roles map and nothing to draw.
    return !out.icons.empty() || !out.palette.empty() || !out.roles.empty();
}

bool IconAtlas::load(rhi::IDevice& device, rhi::ICmdList& cmd, const std::filesystem::path& contentDir,
                     const std::filesystem::path& projectDir)
{
    if (m_texture.valid())
        return true;

    // **What building it cost, in the line that says it was built.** This runs
    // on the first frame that has a command list, so whatever it costs is a
    // frozen window on the way in -- and a number nobody can see is a number
    // nobody improves. Reading a wall clock to report elapsed time is the one
    // thing R10 allows it for, and nothing here reaches the simulation.
    const core::u64 startedNs = platform::nowNs();

    m_sources.clear();
    m_cells.clear();
    m_fallbackId.clear();

    // **The chain, in resolution order.** A project's loose overrides first --
    // one file, no manifest, which is how somebody replaces exactly one icon --
    // and `default` last, which is the only theme that has to be complete.
    // Plugin themes and a project's named theme belong between them and are not
    // built yet; when they are, they are `push_back`s here and nothing else.
    if (!projectDir.empty()) {
        Source overrides;
        overrides.root = projectDir / ".luaug" / "icons";
        overrides.loose = true;
        m_sources.push_back(std::move(overrides));
    }

    Source fallbackTheme;
    if (!readTheme(contentDir / "icons" / "default", fallbackTheme, m_fallbackId)) {
        m_status = "no icon theme found under " + (contentDir / "icons" / "default").string();
        return false;
    }
    m_sources.push_back(std::move(fallbackTheme));

    // **First hit wins, like every other lookup here.** A theme with no block
    // leaves the defaults, which are `default`'s own -- so a plugin that only
    // recolours does not have to restate the badge's geometry to keep it.
    for (const Source& source : m_sources) {
        if (source.overlay.has_value()) {
            m_overlay = *source.overlay;
            break;
        }
    }

    // Every id any source names, so an override for an id `default` does not
    // carry still gets a cell.
    std::vector<std::string> ids;
    for (const Source& source : m_sources) {
        for (const auto& [id, unused] : source.icons) {
            (void)unused;
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());

    // A square atlas big enough for every id at every size. Computed rather
    // than guessed: sixty-seven ids at three sizes is a different number the
    // first time somebody draws six more.
    u32 cellsPerRow = 0;
    u32 atlasSize = 256u;
    for (;;) {
        u32 x = 0;
        u32 y = 0;
        u32 rowHeight = 0;
        bool fits = true;
        for (usize i = 0; i < ids.size() && fits; ++i) {
            for (const u32 size : Sizes) {
                const u32 stride = size + CellPadding * 2u;
                if (x + stride > atlasSize) {
                    x = 0;
                    y += rowHeight;
                    rowHeight = 0;
                }
                if (y + stride > atlasSize) {
                    fits = false;
                    break;
                }
                x += stride;
                rowHeight = std::max(rowHeight, stride);
            }
        }
        if (fits)
            break;
        atlasSize *= 2u;
        if (atlasSize > 4096u) {
            m_status = "the icon set no longer fits a 4096-square atlas";
            return false;
        }
    }
    (void)cellsPerRow;

    std::vector<u8> pixels(static_cast<usize>(atlasSize) * atlasSize * 4u, 0u);
    u32 penX = 0;
    u32 penY = 0;
    u32 rowHeight = 0;
    u32 drawn = 0;
    u32 missing = 0;

    // **Three phases, because eighty-eight decodes in a row is a frozen window.**
    // This runs on the first frame that has a command list, so whatever it costs
    // is paid while somebody is looking at an editor that has not appeared yet --
    // and measured, it cost 131 ms of a 280 ms opening frame.
    //
    // Reading stays serial: the files are a few kilobytes each, and the job pool
    // is explicitly not allowed to block on IO. Decoding and downscaling go wide,
    // which is where the time actually is. Packing stays serial and in `ids`
    // order, so the atlas is byte-identical however many cores ran it -- a
    // layout that depended on scheduling would make two machines disagree about
    // where an icon is.
    struct Encoded
    {
        std::vector<std::byte> bytes;
        bool found = false;
    };
    std::vector<Encoded> encoded(ids.size());

    for (usize index = 0; index < ids.size(); ++index) {
        // The first source that has a file for this id wins, which is the whole
        // of the override rule.
        for (const Source& source : m_sources) {
            std::filesystem::path path;
            if (source.loose) {
                path = source.root / (ids[index] + ".png");
            }
            else {
                const auto it = source.icons.find(ids[index]);
                if (it == source.icons.end())
                    continue;
                path = source.root / std::filesystem::path(it->second);
            }
            if (!platform::readFile(path, encoded[index].bytes))
                continue;
            encoded[index].found = true;
            break;
        }
    }

    struct Built
    {
        std::array<std::vector<u8>, SizeCount> cells;
        bool ok = false;
    };
    std::vector<Built> built(ids.size());

    // One icon per range: they are the same size and there are dozens, so a
    // grain of one is the finest partition and the pool balances it for free.
    // The partition is a function of the data and not of this machine's core
    // count, which `rangeCount` documents and which is what keeps range indices
    // meaning the same thing everywhere.
    jobs::parallelFor("icon-decode", jobs::Domain::AssetIo, 0, ids.size(), 1,
                      [&encoded, &built](usize begin, usize end, u32) noexcept {
                          for (usize index = begin; index < end; ++index) {
                              if (!encoded[index].found)
                                  continue;
                              asset::Image image;
                              if (asset::decodeImage(encoded[index].bytes, image).has_value())
                                  continue;
                              for (usize s = 0; s < SizeCount; ++s)
                                  boxDownscale(image, Sizes[s], built[index].cells[s]);
                              built[index].ok = true;
                          }
                      });

    for (usize index = 0; index < ids.size(); ++index) {
        if (!built[index].ok) {
            // Six `action.` ids are drawn tomorrow and are absent from the theme
            // rather than present and broken, which is what lets this be a count
            // rather than a failure.
            ++missing;
            continue;
        }

        Cell entry;
        for (usize s = 0; s < SizeCount; ++s) {
            const u32 size = Sizes[s];
            const u32 stride = size + CellPadding * 2u;
            if (penX + stride > atlasSize) {
                penX = 0;
                penY += rowHeight;
                rowHeight = 0;
            }

            blit(pixels, atlasSize, built[index].cells[s], size, penX + CellPadding, penY + CellPadding);

            const auto edge = static_cast<f32>(atlasSize);
            entry.sprites[s] = IconSprite{
                static_cast<f32>(penX + CellPadding) / edge,
                static_cast<f32>(penY + CellPadding) / edge,
                static_cast<f32>(penX + CellPadding + size) / edge,
                static_cast<f32>(penY + CellPadding + size) / edge,
                true,
            };

            penX += stride;
            rowHeight = std::max(rowHeight, stride);
        }
        m_cells.emplace(ids[index], entry);
        ++drawn;
    }

    m_texture = device.createTexture({
        .format = rhi::TextureFormat::Rgba8Unorm,
        .usage = rhi::TextureUsage::Sampled,
        .width = atlasSize,
        .height = atlasSize,
        .debugName = "editor-icons",
    });
    if (!m_texture.valid()) {
        m_status = "the icon atlas texture could not be created";
        return false;
    }

    cmd.uploadTexture(m_texture, std::as_bytes(std::span<const u8>(pixels)), 0);
    m_atlasSize = atlasSize;

    const core::u64 elapsedNs = platform::nowNs() - startedNs;
    m_status = std::to_string(drawn) + " icon(s) in a " + std::to_string(atlasSize) + "-square atlas, " +
               std::to_string(elapsedNs / 1000000u) + " ms";
    if (missing > 0)
        m_status += ", " + std::to_string(missing) + " not drawn yet";
    return true;
}

void IconAtlas::destroy(rhi::IDevice& device)
{
    if (m_texture.valid()) {
        // The atlas may be in flight in a frame already submitted, which is the
        // same reason `ViewportTarget::destroy` waits.
        device.waitIdle();
        device.destroy(m_texture);
        m_texture = {};
    }
    m_cells.clear();
    m_sources.clear();
    m_atlasSize = 0;
}

bool IconAtlas::has(std::string_view id) const
{
    return m_cells.find(std::string(id)) != m_cells.end();
}

std::optional<core::Color3> IconAtlas::tintFor(std::string_view id, Panel panel) const
{
    if (!m_tinting)
        return std::nullopt;

    const std::string key(id);

    // The role: the first source in the chain that names this id. A plugin that
    // recolours one class is one entry ahead of `default`'s.
    std::string role;
    for (const Source& source : m_sources) {
        if (const auto it = source.roles.find(key); it != source.roles.end()) {
            role = it->second;
            break;
        }
    }

    // The colour for that role, resolved through the chain separately -- so a
    // plugin may point an id at a role `default` already defines, or define a
    // role of its own, without having to supply both halves.
    const auto colourOf = [this, panel](const std::string& name) -> std::optional<core::Color3> {
        if (name.empty())
            return std::nullopt;
        for (const Source& source : m_sources) {
            if (const auto it = source.palette.find(name); it != source.palette.end())
                return panel == Panel::Dark ? it->second.dark : it->second.light;
        }
        return std::nullopt;
    };

    if (const std::optional<core::Color3> named = colourOf(role); named.has_value())
        return named;

    // **An id nobody names, and a role nobody defines, both land here.** Thirty
    // of the shipped seventy-eight are the first case deliberately -- they are
    // the toolbar's verbs, and a toolbar of ten colours is a fruit salad. The
    // second is a typo in somebody's manifest, and a typo must not make an icon
    // vanish.
    for (const Source& source : m_sources) {
        if (!source.defaultRole.empty()) {
            if (const std::optional<core::Color3> byDefault = colourOf(source.defaultRole); byDefault.has_value())
                return byDefault;
        }
    }
    return std::nullopt;
}

IconSprite IconAtlas::find(std::string_view id, u32 size) const
{
    // The smallest baked size that is at least what was asked for, so a request
    // is always satisfied by shrinking rather than by magnifying -- a mask
    // scaled UP is a blur and a mask scaled down by less than two is not.
    usize level = SizeCount - 1;
    for (usize s = 0; s < SizeCount; ++s) {
        if (Sizes[s] >= size) {
            level = s;
            break;
        }
    }

    auto it = m_cells.find(std::string(id));
    if (it == m_cells.end() && !m_fallbackId.empty())
        it = m_cells.find(m_fallbackId);
    if (it == m_cells.end())
        return {};

    return it->second.sprites[level];
}

} // namespace luaug::app
