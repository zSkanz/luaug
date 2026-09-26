#include "luaug/app/surface_compiler.h"

#include "luaug/asset/surface_shader.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"
#include "luaug/platform/process.h"

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

[[nodiscard]] u64 fnv(u64 hash, std::string_view bytes) noexcept
{
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001B3ull;
    }
    return hash;
}

[[nodiscard]] std::string hex(u64 value)
{
    char text[17]{};
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path)
{
    std::string text;
    if (!platform::readTextFile(path, text))
        return std::nullopt;
    return text;
}

// The files a surface includes, relative to itself, recursively -- what the
// cache key has to cover. `luaug/...` is the engine's and versioned by the
// contract; anything not found beside the file is left for the compiler to
// report.
void collectIncludes(const std::filesystem::path& file, std::string_view text, std::vector<std::filesystem::path>& into)
{
    for (std::size_t at = text.find("#include"); at != std::string_view::npos; at = text.find("#include", at + 8)) {
        const std::size_t open = text.find('"', at);
        const std::size_t end = text.find('\n', at);
        if (open == std::string_view::npos || (end != std::string_view::npos && open > end))
            continue;
        const std::size_t close = text.find('"', open + 1);
        if (close == std::string_view::npos)
            continue;
        const std::string_view name = text.substr(open + 1, close - open - 1);
        if (name.starts_with("luaug/"))
            continue;
        std::error_code error;
        const std::filesystem::path included =
            std::filesystem::weakly_canonical(file.parent_path() / std::filesystem::path(name), error);
        if (error || !std::filesystem::exists(included, error) ||
            std::find(into.begin(), into.end(), included) != into.end())
            continue;
        into.push_back(included);
        if (const std::optional<std::string> nested = readText(included))
            collectIncludes(included, *nested, into);
    }
}

// `file:line:column: error: message`, as DXC writes it. The wrapper's own
// lines are the engine's fault, not the user's, and are kept as they are.
[[nodiscard]] std::vector<SurfaceError> parseErrors(std::string_view output)
{
    std::vector<SurfaceError> errors;
    std::size_t start = 0;
    while (start < output.size()) {
        std::size_t end = output.find('\n', start);
        if (end == std::string_view::npos)
            end = output.size();
        std::string_view line = output.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const std::size_t marker = line.find(": error: ");
        if (marker == std::string_view::npos)
            continue;
        SurfaceError error;
        error.message = std::string(line.substr(marker + 9));
        std::string_view location = line.substr(0, marker);
        // Two numbers from the end, separated by colons: line and column.
        const std::size_t column = location.rfind(':');
        const std::size_t row = column == std::string_view::npos ? column : location.rfind(':', column - 1);
        if (row != std::string_view::npos) {
            const std::string_view number = location.substr(row + 1, column - row - 1);
            (void)std::from_chars(number.data(), number.data() + number.size(), error.line);
            error.file = std::string(location.substr(0, row));
        }
        else {
            error.file = std::string(location);
        }
        errors.push_back(std::move(error));
    }
    if (errors.empty() && !output.empty())
        errors.push_back(SurfaceError{"", 0, std::string(output.substr(0, 2000))});
    return errors;
}

[[nodiscard]] std::string_view destination(rhi::ShaderFormat format) noexcept
{
    switch (format) {
    case rhi::ShaderFormat::Dxil:
        return "DXIL";
    case rhi::ShaderFormat::Msl:
        return "MSL";
    default:
        return "SPIRV";
    }
}

} // namespace

SurfaceCompiler::SurfaceCompiler(const asset::ContentMounts& mounts, std::filesystem::path shadercross,
                                 std::filesystem::path includeDirectory, std::filesystem::path cache)
    : m_mounts(mounts), m_shadercross(std::move(shadercross)), m_include(std::move(includeDirectory)),
      m_cache(std::move(cache))
{
    // The engine's headers, hashed once: they change with the engine, not
    // while it runs.
    std::error_code error;
    std::vector<std::filesystem::path> headers;
    for (std::filesystem::recursive_directory_iterator it(m_include, error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_regular_file(error))
            headers.push_back(it->path());
    }
    std::sort(headers.begin(), headers.end());
    m_headers = 0xCBF29CE484222325ull;
    for (const std::filesystem::path& header : headers)
        m_headers = fnv(fnv(m_headers, header.filename().string()), readText(header).value_or(std::string{}));
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

    const asset::ResolvedContent resolved = m_mounts.resolve(urn);
    if (resolved.source != asset::ResolvedContent::Source::Loose) {
        errors.push_back(SurfaceError{urn, 0, "not a file this editor can compile"});
        finish(false);
        return;
    }
    std::error_code fsError;
    const std::filesystem::path file = std::filesystem::weakly_canonical(resolved.path, fsError);
    const std::optional<std::string> source = readText(file);
    if (!source.has_value()) {
        errors.push_back(SurfaceError{file.string(), 0, "cannot be read"});
        finish(false);
        return;
    }

    std::vector<std::filesystem::path> files{file};
    collectIncludes(file, *source, files);
    u64 key = fnv(0xCBF29CE484222325ull, std::to_string(asset::SurfaceContractVersion));
    key = fnv(key, destination(format));
    for (const std::filesystem::path& path : files) {
        dependencies.emplace_back(path, std::filesystem::last_write_time(path, fsError));
        key = fnv(key, readText(path).value_or(std::string{}));
    }

    program->reflection = asset::reflectSurface(*source);
    if (program->reflection.ok()) {
        // **And everything the engine wraps it in**: the generated text and the
        // engine's headers. A key of the user's files alone kept serving
        // bytecode compiled around an older wrapper after the engine changed.
        for (u32 variant = 0; variant < 5; ++variant) {
            for (const bool fragment : {false, true}) {
                key = fnv(key,
                          asset::surfaceWrapper(program->reflection, static_cast<asset::SurfaceVariant>(variant),
                                                fragment ? asset::SurfaceStage::Fragment : asset::SurfaceStage::Vertex,
                                                file.generic_string()));
            }
        }
        key ^= m_headers;
    }
    if (!program->reflection.ok()) {
        for (const asset::SurfaceDiagnostic& diagnostic : program->reflection.errors) {
            const std::array<core::I18nArg, 1> args{core::I18nArg{"subject", diagnostic.subject}};
            errors.push_back(
                SurfaceError{file.generic_string(), diagnostic.line,
                             core::engineCatalog().format(core::TextKey{core::hashTextKey(diagnostic.key)}, args)});
        }
        finish(false);
        return;
    }

    // **Cached by what it was built from**: a directory per key, the wrappers
    // and the bytecode in it. A second ask -- another session, the same file
    // -- reads the bytecode back and runs nothing.
    const std::filesystem::path directory = m_cache / hex(key);
    std::filesystem::create_directories(directory, fsError);
    constexpr std::array<std::string_view, 5> Variants{"forward", "forward_instanced", "forward_blended", "depth",
                                                       "depth_instanced"};
    for (u32 variant = 0; variant < Variants.size(); ++variant) {
        for (const bool fragment : {false, true}) {
            const std::string stem = std::string(Variants[variant]) + (fragment ? ".fragment" : ".vertex");
            const std::filesystem::path wrapper = directory / (stem + ".hlsl");
            const std::filesystem::path output = directory / (stem + ".bin");
            std::vector<std::byte>& code = program->code[variant * 2 + (fragment ? 1 : 0)];
            if (platform::readFile(output, code) && !code.empty())
                continue;
            const std::string text = asset::surfaceWrapper(
                program->reflection, static_cast<asset::SurfaceVariant>(variant),
                fragment ? asset::SurfaceStage::Fragment : asset::SurfaceStage::Vertex, file.generic_string());
            if (!platform::writeTextFile(wrapper, text)) {
                errors.push_back(SurfaceError{wrapper.string(), 0, "cannot be written"});
                finish(false);
                return;
            }
            const platform::ProcessResult result = platform::runProcess({
                m_shadercross.string(),
                wrapper.string(),
                "-s",
                "HLSL",
                "-d",
                std::string(destination(format)),
                "-t",
                fragment ? "fragment" : "vertex",
                "-e",
                fragment ? "FragmentMain" : "VertexMain",
                "-I",
                m_include.string(),
                "-o",
                output.string(),
            });
            ++compiled;
            if (!result.started || result.exitCode != 0 || !platform::readFile(output, code) || code.empty()) {
                errors = parseErrors(result.output);
                std::filesystem::remove(output, fsError);
                finish(false);
                return;
            }
        }
    }
    finish(true);
}

} // namespace luaug::app
