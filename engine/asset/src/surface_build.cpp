#include "luaug/asset/surface_build.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"
#include "luaug/platform/process.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace luaug::asset {

using core::u32;
using core::u64;
using core::usize;

namespace {

constexpr u64 FnvBasis = 0xCBF29CE484222325ull;

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

// Every `#include "x"` a file reaches that is the project's -- the engine's own
// (`luaug/...`) are the headers hash's business.
void collectIncludes(const std::filesystem::path& file, std::string_view text, std::vector<std::filesystem::path>& into)
{
    for (usize at = text.find("#include"); at != std::string_view::npos; at = text.find("#include", at + 8)) {
        const usize open = text.find('"', at);
        const usize end = text.find('\n', at);
        if (open == std::string_view::npos || (end != std::string_view::npos && open > end))
            continue;
        const usize close = text.find('"', open + 1);
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

constexpr std::array<std::string_view, 5> VariantStems{"forward", "forward_instanced", "forward_blended", "depth",
                                                       "depth_instanced"};

} // namespace

std::string_view surfaceTargetName(SurfaceTarget target) noexcept
{
    switch (target) {
    case SurfaceTarget::Dxil:
        return "DXIL";
    case SurfaceTarget::Msl:
        return "MSL";
    case SurfaceTarget::Spirv:
        break;
    }
    return "SPIRV";
}

u64 surfaceHeadersHash(const std::filesystem::path& include)
{
    std::error_code error;
    std::vector<std::filesystem::path> headers;
    for (std::filesystem::recursive_directory_iterator it(include, error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_regular_file(error))
            headers.push_back(it->path());
    }
    std::sort(headers.begin(), headers.end());
    u64 hash = FnvBasis;
    for (const std::filesystem::path& header : headers)
        hash = fnv(fnv(hash, header.filename().string()), readText(header).value_or(std::string{}));
    return hash;
}

std::vector<SurfaceBuildError> parseSurfaceErrors(std::string_view output)
{
    std::vector<SurfaceBuildError> errors;
    usize start = 0;
    while (start < output.size()) {
        usize end = output.find('\n', start);
        if (end == std::string_view::npos)
            end = output.size();
        std::string_view line = output.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const usize marker = line.find(": error: ");
        if (marker == std::string_view::npos)
            continue;
        SurfaceBuildError error;
        error.message = std::string(line.substr(marker + 9));
        const std::string_view location = line.substr(0, marker);
        // Two numbers from the end, separated by colons: line and column.
        const usize column = location.rfind(':');
        const usize row = column == std::string_view::npos ? column : location.rfind(':', column - 1);
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
        errors.push_back(SurfaceBuildError{"", 0, std::string(output.substr(0, 2000))});
    return errors;
}

SurfaceBuild buildSurface(const SurfaceBuildInputs& inputs, SurfaceTarget target)
{
    SurfaceBuild build;
    std::error_code fsError;
    const std::filesystem::path file = std::filesystem::weakly_canonical(inputs.source, fsError);
    const std::optional<std::string> source = readText(file);
    if (!source.has_value()) {
        build.errors.push_back(SurfaceBuildError{file.generic_string(), 0, "cannot be read"});
        return build;
    }

    build.files.push_back(file);
    collectIncludes(file, *source, build.files);
    u64 key = fnv(FnvBasis, std::to_string(SurfaceContractVersion));
    key = fnv(key, surfaceTargetName(target));
    for (const std::filesystem::path& path : build.files)
        key = fnv(key, readText(path).value_or(std::string{}));

    build.reflection = reflectSurface(*source);
    if (!build.reflection.ok()) {
        for (const SurfaceDiagnostic& diagnostic : build.reflection.errors) {
            const std::array<core::I18nArg, 1> args{core::I18nArg{"subject", diagnostic.subject}};
            build.errors.push_back(SurfaceBuildError{
                file.generic_string(), diagnostic.line,
                core::engineCatalog().format(core::TextKey{core::hashTextKey(diagnostic.key)}, args)});
        }
        return build;
    }

    // **And everything the engine wraps it in**: the generated text and the
    // engine's headers. A key of the user's files alone kept serving bytecode
    // compiled around an older wrapper after the engine changed.
    const auto wrapperOf = [&](u32 variant, bool fragment) {
        return surfaceWrapper(build.reflection, static_cast<SurfaceVariant>(variant),
                              fragment ? SurfaceStage::Fragment : SurfaceStage::Vertex, file.generic_string());
    };
    for (u32 variant = 0; variant < VariantStems.size(); ++variant) {
        for (const bool fragment : {false, true})
            key = fnv(key, wrapperOf(variant, fragment));
    }
    key ^= inputs.headers;

    // A directory per key, the wrappers and the bytecode in it: a second ask --
    // another session, another build of the same project -- reads the bytecode
    // back and runs nothing.
    const std::filesystem::path directory = inputs.cache / hex(key);
    std::filesystem::create_directories(directory, fsError);
    for (u32 variant = 0; variant < VariantStems.size(); ++variant) {
        for (const bool fragment : {false, true}) {
            const std::string stem = std::string(VariantStems[variant]) + (fragment ? ".fragment" : ".vertex");
            const std::filesystem::path wrapper = directory / (stem + ".hlsl");
            const std::filesystem::path output = directory / (stem + ".bin");
            std::vector<std::byte>& code = build.code[variant * 2 + (fragment ? 1 : 0)];
            if (platform::readFile(output, code) && !code.empty())
                continue;
            if (!platform::writeTextFile(wrapper, wrapperOf(variant, fragment))) {
                build.errors.push_back(SurfaceBuildError{wrapper.generic_string(), 0, "cannot be written"});
                return build;
            }
            const platform::ProcessResult result = platform::runProcess({
                inputs.shadercross.string(),
                wrapper.string(),
                "-s",
                "HLSL",
                "-d",
                std::string(surfaceTargetName(target)),
                "-t",
                fragment ? "fragment" : "vertex",
                "-e",
                fragment ? "FragmentMain" : "VertexMain",
                "-I",
                inputs.include.string(),
                "-o",
                output.string(),
            });
            ++build.compiled;
            if (!result.started || result.exitCode != 0 || !platform::readFile(output, code) || code.empty()) {
                build.errors = parseSurfaceErrors(result.output);
                if (build.errors.empty())
                    build.errors.push_back(SurfaceBuildError{"", 0, "the shader compiler did not run"});
                std::filesystem::remove(output, fsError);
                return build;
            }
        }
    }
    build.ok = true;
    return build;
}

// --- In a pack ---------------------------------------------------------------

namespace {

constexpr std::array<char, 4> SurfaceMagic{'L', 'S', 'R', 'F'};

void putWord(std::vector<std::byte>& out, u32 value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
}

void putBytes(std::vector<std::byte>& out, std::span<const std::byte> bytes)
{
    putWord(out, static_cast<u32>(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
}

class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes) : m_bytes(bytes) {}
    [[nodiscard]] bool word(u32& out)
    {
        if (m_at + 4 > m_bytes.size())
            return false;
        out = 0;
        for (usize index = 0; index < 4; ++index)
            out |= static_cast<u32>(m_bytes[m_at + index]) << (8 * index);
        m_at += 4;
        return true;
    }
    [[nodiscard]] bool bytes(std::vector<std::byte>& out)
    {
        u32 size = 0;
        if (!word(size) || m_at + size > m_bytes.size())
            return false;
        out.assign(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_at),
                   m_bytes.begin() + static_cast<std::ptrdiff_t>(m_at + size));
        m_at += size;
        return true;
    }
    [[nodiscard]] bool done() const noexcept { return m_at == m_bytes.size(); }

private:
    std::span<const std::byte> m_bytes;
    usize m_at = 0;
};

} // namespace

const SurfaceCode* CompiledSurface::code(SurfaceTarget target) const noexcept
{
    for (const auto& [which, code] : targets) {
        if (which == target)
            return &code;
    }
    return nullptr;
}

std::vector<std::byte> encodeSurface(const CompiledSurface& surface)
{
    std::vector<std::byte> out;
    for (const char c : SurfaceMagic)
        out.push_back(static_cast<std::byte>(c));
    putWord(out, CompiledSurfaceVersion);
    putWord(out, SurfaceContractVersion);
    putBytes(out, std::as_bytes(std::span<const char>(surface.source.data(), surface.source.size())));
    putWord(out, static_cast<u32>(surface.targets.size()));
    for (const auto& [target, code] : surface.targets) {
        putWord(out, static_cast<u32>(target));
        for (const std::vector<std::byte>& stage : code)
            putBytes(out, stage);
    }
    return out;
}

std::optional<CompiledSurface> decodeSurface(std::span<const std::byte> bytes)
{
    if (bytes.size() < SurfaceMagic.size() || std::memcmp(bytes.data(), SurfaceMagic.data(), SurfaceMagic.size()) != 0)
        return std::nullopt;
    Reader in(bytes.subspan(SurfaceMagic.size()));
    u32 version = 0;
    u32 contract = 0;
    std::vector<std::byte> source;
    u32 count = 0;
    if (!in.word(version) || version != CompiledSurfaceVersion || !in.word(contract) ||
        contract != SurfaceContractVersion || !in.bytes(source) || !in.word(count) || count > AllSurfaceTargets.size())
        return std::nullopt;
    CompiledSurface surface;
    surface.source.assign(reinterpret_cast<const char*>(source.data()), source.size());
    for (u32 index = 0; index < count; ++index) {
        u32 target = 0;
        if (!in.word(target) || target >= AllSurfaceTargets.size())
            return std::nullopt;
        SurfaceCode code;
        for (std::vector<std::byte>& stage : code) {
            if (!in.bytes(stage))
                return std::nullopt;
        }
        surface.targets.emplace_back(static_cast<SurfaceTarget>(target), std::move(code));
    }
    if (!in.done())
        return std::nullopt;
    return surface;
}

} // namespace luaug::asset
