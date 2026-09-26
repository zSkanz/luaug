#pragma once

// **The surface shaders a built game carries** (ADR 0091): compiled by
// `assetc` into the pack, read back here, and never compiled. What a player
// draws a user's surface with, and what the editor falls back to for a surface
// that is only in a pack.

#include "luaug/asset/content.h"
#include "luaug/render/surface_source.h"

#include <map>
#include <memory>
#include <string>

namespace luaug::render {

class PackSurfaceSource final : public ISurfaceSource
{
public:
    // `mounts` outlives this; a surface's bytes are read from it once.
    explicit PackSurfaceSource(const asset::ContentMounts& mounts) : m_mounts(mounts) {}

    // Ready when the pack holds `urn` compiled for `format`. Failed -- the
    // error surface, and one line in the log -- when it holds it for another
    // target only, or when `urn` is a loose file nothing here can compile.
    [[nodiscard]] SurfaceStatus find(std::string_view urn, rhi::ShaderFormat format,
                                     const SurfaceProgram*& program) override;

private:
    struct Entry
    {
        SurfaceStatus status = SurfaceStatus::Failed;
        std::unique_ptr<SurfaceProgram> program;
    };

    const asset::ContentMounts& m_mounts;
    std::map<std::string, Entry, std::less<>> m_entries;
};

} // namespace luaug::render
