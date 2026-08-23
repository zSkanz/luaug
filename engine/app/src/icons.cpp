#include "luaug/app/icons.h"

#include "luaug/asset/image.h"
#include "luaug/core/json.h"
#include "luaug/platform/file.h"
#include "luaug/rhi/descs.h"
#include "luaug/rhi/device.h"

#include <algorithm>
#include <cstring>

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

    // The last theme consulted supplies the fallback, which for a chain that
    // always ends at `default` means `default`'s.
    if (const std::string_view declared = root["fallback"].asString(); !declared.empty())
        fallbackId = std::string(declared);

    out.root = themeDir;
    return !out.icons.empty();
}

bool IconAtlas::load(rhi::IDevice& device, rhi::ICmdList& cmd, const std::filesystem::path& contentDir,
                     const std::filesystem::path& projectDir)
{
    if (m_texture.valid())
        return true;

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
    std::vector<u8> cell;
    u32 penX = 0;
    u32 penY = 0;
    u32 rowHeight = 0;
    u32 drawn = 0;
    u32 missing = 0;

    for (const std::string& id : ids) {
        // The first source that has a file for this id wins, which is the whole
        // of the override rule.
        asset::Image image;
        bool decoded = false;
        for (const Source& source : m_sources) {
            std::filesystem::path path;
            if (source.loose) {
                path = source.root / (id + ".png");
            }
            else {
                const auto it = source.icons.find(id);
                if (it == source.icons.end())
                    continue;
                path = source.root / std::filesystem::path(it->second);
            }

            std::vector<std::byte> encoded;
            if (!platform::readFile(path, encoded))
                continue;
            if (asset::decodeImage(encoded, image).has_value())
                continue;
            decoded = true;
            break;
        }
        if (!decoded) {
            // Six `action.` ids are drawn tomorrow and are absent from the
            // theme rather than present and broken, which is what lets this be
            // a count rather than a failure.
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

            boxDownscale(image, size, cell);
            blit(pixels, atlasSize, cell, size, penX + CellPadding, penY + CellPadding);

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
        m_cells.emplace(id, entry);
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

    m_status = std::to_string(drawn) + " icon(s) in a " + std::to_string(atlasSize) + "-square atlas";
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
