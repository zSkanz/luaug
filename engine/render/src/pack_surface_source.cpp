#include "luaug/render/pack_surface_source.h"

#include "luaug/asset/surface_build.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"

#include <array>

namespace luaug::render {

namespace {

[[nodiscard]] asset::SurfaceTarget targetOf(rhi::ShaderFormat format) noexcept
{
    switch (format) {
    case rhi::ShaderFormat::Dxil:
        return asset::SurfaceTarget::Dxil;
    case rhi::ShaderFormat::Msl:
        return asset::SurfaceTarget::Msl;
    case rhi::ShaderFormat::SpirV:
    case rhi::ShaderFormat::Unknown:
        break;
    }
    return asset::SurfaceTarget::Spirv;
}

} // namespace

SurfaceStatus PackSurfaceSource::find(std::string_view urn, rhi::ShaderFormat format, const SurfaceProgram*& program)
{
    program = nullptr;
    if (const auto found = m_entries.find(urn); found != m_entries.end()) {
        program = found->second.program.get();
        return found->second.status;
    }

    // Decided once per surface: a pack does not change under a running game.
    Entry& entry = m_entries[std::string(urn)];
    const asset::ResolvedContent resolved = m_mounts.resolve(urn);
    std::optional<asset::CompiledSurface> compiled;
    if (resolved.source == asset::ResolvedContent::Source::Pack && resolved.kind == asset::AssetKind::Surface)
        compiled = asset::decodeSurface(resolved.bytes);

    const asset::SurfaceCode* code = compiled.has_value() ? compiled->code(targetOf(format)) : nullptr;
    if (code == nullptr) {
        // A loose file (nothing here compiles), a pack built with no compiler
        // for this backend, or not a surface at all: the error surface, said
        // once.
        const std::array<core::I18nArg, 2> args{core::I18nArg{"urn", urn},
                                                core::I18nArg{"target", asset::surfaceTargetName(targetOf(format))}};
        core::log(core::LogLevel::Error, LUAUG_TR("render.err.surface_not_packed"), args);
        entry.status = SurfaceStatus::Failed;
        return entry.status;
    }

    auto made = std::make_unique<SurfaceProgram>();
    made->reflection = asset::reflectSurface(compiled->source);
    made->code = *code;
    made->revision = 1;
    entry.status = made->reflection.ok() ? SurfaceStatus::Ready : SurfaceStatus::Failed;
    entry.program = std::move(made);
    program = entry.program.get();
    return entry.status;
}

} // namespace luaug::render
