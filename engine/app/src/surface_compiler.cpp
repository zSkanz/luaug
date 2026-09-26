#include "luaug/app/surface_compiler.h"

#include "luaug/asset/surface_build.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <optional>
#include <system_error>

namespace luaug::app {

using core::u32;
using core::u64;

namespace {

// Half a second between looks at a surface's files: a save is seen at once to
// a person, and a hundred materials naming it cost a hundred stats a second.
constexpr auto RecheckInterval = std::chrono::milliseconds(500);

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

SurfaceCompiler::SurfaceCompiler(const asset::ContentMounts& mounts, std::filesystem::path shadercross,
                                 std::filesystem::path includeDirectory, std::filesystem::path cache)
    : m_mounts(mounts), m_shadercross(std::move(shadercross)), m_include(std::move(includeDirectory)),
      m_cache(std::move(cache))
{
    // The engine's headers, hashed once: they change with the engine, not
    // while it runs.
    m_headers = asset::surfaceHeadersHash(m_include);
    m_worker = std::thread([this] { work(); });
}

SurfaceCompiler::~SurfaceCompiler()
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

bool SurfaceCompiler::available() const noexcept
{
    std::error_code error;
    return std::filesystem::exists(m_shadercross, error);
}

render::SurfaceStatus SurfaceCompiler::find(std::string_view urn, rhi::ShaderFormat format,
                                            const render::SurfaceProgram*& program)
{
    program = nullptr;
    const std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_entries.find(urn);
    if (found == m_entries.end()) {
        Entry entry;
        entry.urn = std::string(urn);
        entry.format = format;
        entry.queued = true;
        found = m_entries.emplace(entry.urn, std::move(entry)).first;
        m_queue.push_back(found->first);
        m_wake.notify_one();
        return render::SurfaceStatus::Pending;
    }
    Entry& entry = found->second;

    // **A saved file is a recompile**: the source or anything it includes.
    const auto now = std::chrono::steady_clock::now();
    if (!entry.queued && entry.status != render::SurfaceStatus::Pending && now - entry.checked > RecheckInterval) {
        entry.checked = now;
        const bool changed =
            std::any_of(entry.dependencies.begin(), entry.dependencies.end(), [](const auto& dependency) {
                std::error_code error;
                const std::filesystem::file_time_type written =
                    std::filesystem::last_write_time(dependency.first, error);
                return error || written != dependency.second;
            });
        if (changed) {
            entry.queued = true;
            m_queue.push_back(entry.urn);
            m_wake.notify_one();
        }
    }
    if (entry.status == render::SurfaceStatus::Ready)
        program = entry.program.get();
    return entry.status;
}

std::vector<SurfaceError> SurfaceCompiler::errors(std::string_view urn) const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_entries.find(urn);
    return found != m_entries.end() ? found->second.errors : std::vector<SurfaceError>{};
}

std::optional<render::SurfaceStatus> SurfaceCompiler::status(std::string_view urn) const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_entries.find(urn);
    if (found == m_entries.end())
        return std::nullopt;
    return found->second.status;
}

void SurfaceCompiler::drain()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_idle.wait(lock, [this] { return m_queue.empty() && !m_busy; });
}

void SurfaceCompiler::work()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
        if (m_stop)
            return;
        const std::string urn = m_queue.front();
        m_queue.pop_front();
        const rhi::ShaderFormat format = m_entries[urn].format;
        m_busy = true;
        lock.unlock();
        compile(urn, format);
        lock.lock();
        m_busy = false;
        if (m_queue.empty())
            m_idle.notify_all();
    }
}

void SurfaceCompiler::compile(const std::string& urn, rhi::ShaderFormat format)
{
    const auto started = std::chrono::steady_clock::now();
    u32 compiled = 0;
    std::vector<SurfaceError> errors;
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> dependencies;
    auto program = std::make_unique<render::SurfaceProgram>();

    const auto finish = [&](bool ok) {
        for (const SurfaceError& error : errors) {
            const std::array<core::I18nArg, 3> args{core::I18nArg{"file", error.file.empty() ? urn : error.file},
                                                    core::I18nArg{"line", static_cast<core::i64>(error.line)},
                                                    core::I18nArg{"message", error.message}};
            core::log(core::LogLevel::Error, LUAUG_TR("render.err.surface_compile"), args);
        }
        const std::lock_guard<std::mutex> lock(m_mutex);
        Entry& entry = m_entries[urn];
        entry.queued = false;
        entry.checked = std::chrono::steady_clock::now();
        entry.errors = std::move(errors);
        if (!dependencies.empty())
            entry.dependencies = std::move(dependencies);
        if (ok) {
            // How long it took, and how much of it was the compiler: a cache
            // hit runs nothing, and a person tuning a shader wants to know.
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
            const std::array<core::I18nArg, 3> timing{
                core::I18nArg{"urn", urn}, core::I18nArg{"milliseconds", static_cast<core::i64>(elapsed.count())},
                core::I18nArg{"compiled", static_cast<core::i64>(compiled)}};
            core::log(core::LogLevel::Info, LUAUG_TR("render.info.surface_compiled"), timing);
            program->revision = ++entry.revision;
            entry.program = std::move(program);
            entry.status = render::SurfaceStatus::Ready;
        }
        else {
            entry.program.reset();
            entry.status = render::SurfaceStatus::Failed;
        }
    };

    const asset::ResolvedContent resolved = m_mounts.resolve(urn);

    // **A surface that is only in a pack** -- a project opened from its build
    // -- is read back rather than compiled: its bytecode is already there, and
    // no compiler is needed to use it.
    if (resolved.source == asset::ResolvedContent::Source::Pack) {
        const std::optional<asset::CompiledSurface> packed =
            resolved.kind == asset::AssetKind::Surface ? asset::decodeSurface(resolved.bytes) : std::nullopt;
        const asset::SurfaceCode* code = packed.has_value() ? packed->code(targetOf(format)) : nullptr;
        if (code == nullptr) {
            errors.push_back(SurfaceError{urn, 0, "the pack holds no bytecode of it for this backend"});
            finish(false);
            return;
        }
        program->reflection = asset::reflectSurface(packed->source);
        program->code = *code;
        finish(program->reflection.ok());
        return;
    }

    if (!available()) {
        bool warn = false;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            warn = !m_warned;
            m_warned = true;
        }
        if (warn) {
            const std::array<core::I18nArg, 1> args{core::I18nArg{"path", m_shadercross.string()}};
            core::log(core::LogLevel::Error, LUAUG_TR("render.err.surface_no_compiler"), args);
        }
        finish(false);
        return;
    }

    if (resolved.source != asset::ResolvedContent::Source::Loose) {
        errors.push_back(SurfaceError{urn, 0, "not a file this editor can compile"});
        finish(false);
        return;
    }

    const asset::SurfaceBuild build = asset::buildSurface(
        asset::SurfaceBuildInputs{resolved.path, m_shadercross, m_include, m_cache, m_headers}, targetOf(format));
    compiled = build.compiled;
    std::error_code fsError;
    for (const std::filesystem::path& path : build.files)
        dependencies.emplace_back(path, std::filesystem::last_write_time(path, fsError));
    for (const asset::SurfaceBuildError& error : build.errors)
        errors.push_back(SurfaceError{error.file, error.line, error.message});
    program->reflection = build.reflection;
    program->code = build.code;
    finish(build.ok);
}

} // namespace luaug::app
