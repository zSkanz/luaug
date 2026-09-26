// The editor's surface compiler (ADR 0091) against the real `shadercross` and
// the engine's real headers: a surface compiles into every variant, a broken
// one fails with its line, and a saved fix is picked up and compiled again.
#include "luaug/app/surface_compiler.h"
#include "luaug/assetc/compiler.h"
#include "luaug/core/i18n.h"
#include "luaug/platform/file.h"
#include "luaug/render/pack_surface_source.h"

#include <chrono>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace luaug;

namespace {

struct Folder
{
    std::filesystem::path root;
    Folder()
    {
        root = std::filesystem::temp_directory_path() /
               ("luaug-surface-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "content" / "shaders");
    }
    ~Folder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void write(const std::string& relative, const std::string& text) const
    {
        std::ofstream out(root / "content" / relative, std::ios::binary);
        out << text;
    }
};

[[nodiscard]] render::SurfaceStatus settle(app::SurfaceCompiler& compiler, std::string_view urn,
                                           const render::SurfaceProgram*& program)
{
    (void)compiler.find(urn, rhi::ShaderFormat::SpirV, program);
    compiler.drain();
    return compiler.find(urn, rhi::ShaderFormat::SpirV, program);
}

constexpr std::string_view Wave = R"(#include "luaug/surface.hlsli"
LUAUG_PARAM(float, Height, 0.5, range(0, 2))
LUAUG_TEXTURE(Foam)
void surfaceVertex(inout SurfaceVertex vertex, SurfaceInputs inputs)
{
    vertex.Position.y += sin(inputs.Time + vertex.Position.x) * Height;
}
void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)
{
    surface.BaseColor = LUAUG_SAMPLE(Foam, inputs.Uv0).rgb;
}
)";

} // namespace

TEST_CASE("a surface compiles into every variant, fails with its line, and a saved fix compiles again")
{
    if (std::string_view(LUAUG_TEST_SHADERCROSS).empty() || !std::filesystem::exists(LUAUG_TEST_SHADERCROSS)) {
        MESSAGE("LUAUG_TEST_SKIP: no shader toolchain on this host");
        return;
    }
    REQUIRE(core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG).ok);
    Folder folder;
    folder.write("shaders/wave.surface.hlsl", std::string(Wave));
    asset::ContentMounts mounts;
    mounts.mountDirectory(folder.root / "content");
    app::SurfaceCompiler compiler(mounts, LUAUG_TEST_SHADERCROSS, LUAUG_TEST_SHADER_INCLUDE, folder.root / "cache");
    CHECK(compiler.available());

    const std::string urn = "asset://shaders/wave.surface.hlsl";
    const render::SurfaceProgram* program = nullptr;
    // The first ask never waits.
    CHECK(compiler.find(urn, rhi::ShaderFormat::SpirV, program) == render::SurfaceStatus::Pending);
    CHECK(program == nullptr);
    compiler.drain();
    REQUIRE(compiler.find(urn, rhi::ShaderFormat::SpirV, program) == render::SurfaceStatus::Ready);
    REQUIRE(program != nullptr);
    for (const std::vector<std::byte>& code : program->code)
        CHECK_FALSE(code.empty());
    CHECK(program->reflection.param("Height") != nullptr);
    const core::u64 first = program->revision;

    // Broken: the error names the line it is on.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    folder.write("shaders/wave.surface.hlsl", std::string(Wave).replace(Wave.find("Height;"), 7, "Heigth;"));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    CHECK(settle(compiler, urn, program) == render::SurfaceStatus::Failed);
    const std::vector<app::SurfaceError> errors = compiler.errors(urn);
    REQUIRE_FALSE(errors.empty());
    CHECK(errors.front().line == 6);

    // Fixed: compiled again, under a new revision the renderer rebuilds from.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    folder.write("shaders/wave.surface.hlsl", std::string(Wave));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    REQUIRE(settle(compiler, urn, program) == render::SurfaceStatus::Ready);
    CHECK(program->revision > first);
    CHECK(compiler.errors(urn).empty());
}

namespace {

// Builds `folder`'s content into a pack and mounts it -- the pack alone, as a
// player has it.
[[nodiscard]] assetc::CompileResult buildPack(const Folder& folder, const std::filesystem::path& shadercross,
                                              asset::ContentMounts& mounts)
{
    assetc::CompileOptions options;
    options.inputRoot = folder.root / "content";
    options.cacheRoot = folder.root / "cache";
    options.shadercross = shadercross;
    options.surfaceInclude = LUAUG_TEST_SHADER_INCLUDE;
    assetc::CompileResult result = assetc::compile(options);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
    std::string diagnostic;
    const std::filesystem::path pack = folder.root / "game.lpack";
    const std::filesystem::path manifest = folder.root / "game.manifest.json";
    REQUIRE(assetc::writeFile(pack, result.pack, diagnostic));
    REQUIRE(assetc::writeFile(
        manifest, std::as_bytes(std::span<const char>(result.manifest.data(), result.manifest.size())), diagnostic));
    REQUIRE_FALSE(mounts.mountPack(pack, manifest).has_value());
    return result;
}

} // namespace

TEST_CASE("a built game carries its surfaces compiled for every target, and draws them with no compiler")
{
    if (std::string_view(LUAUG_TEST_SHADERCROSS).empty() || !std::filesystem::exists(LUAUG_TEST_SHADERCROSS)) {
        MESSAGE("LUAUG_TEST_SKIP: no shader toolchain on this host");
        return;
    }
    REQUIRE(core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG).ok);
    Folder folder;
    folder.write("shaders/wave.surface.hlsl", std::string(Wave));
    asset::ContentMounts mounts;
    const assetc::CompileResult result = buildPack(folder, LUAUG_TEST_SHADERCROSS, mounts);
    CHECK(result.surfaceCount == 1);
    CHECK(result.surfacesUncompiled == 0);

    // The player's source: every backend's bytecode is there, and nothing runs.
    const std::string urn = "asset://shaders/wave.surface.hlsl";
    for (const rhi::ShaderFormat format : {rhi::ShaderFormat::SpirV, rhi::ShaderFormat::Dxil, rhi::ShaderFormat::Msl}) {
        render::PackSurfaceSource source(mounts);
        const render::SurfaceProgram* program = nullptr;
        REQUIRE(source.find(urn, format, program) == render::SurfaceStatus::Ready);
        REQUIRE(program != nullptr);
        for (const std::vector<std::byte>& code : program->code)
            CHECK_FALSE(code.empty());
        CHECK(program->reflection.param("Height") != nullptr);
    }

    // The editor over a project it only has the build of: read back, not
    // compiled -- it needs no compiler to draw it.
    app::SurfaceCompiler compiler(mounts, folder.root / "no-compiler", LUAUG_TEST_SHADER_INCLUDE,
                                  folder.root / "editor-cache");
    const render::SurfaceProgram* program = nullptr;
    CHECK(settle(compiler, urn, program) == render::SurfaceStatus::Ready);
}

TEST_CASE("a game built with no compiler still packs its surfaces, and draws them as the error surface")
{
    Folder folder;
    folder.write("shaders/wave.surface.hlsl", std::string(Wave));
    asset::ContentMounts mounts;
    const assetc::CompileResult result = buildPack(folder, folder.root / "no-compiler", mounts);
    CHECK(result.surfaceCount == 1);
    CHECK(result.surfacesUncompiled == 1);

    render::PackSurfaceSource source(mounts);
    const render::SurfaceProgram* program = nullptr;
    CHECK(source.find("asset://shaders/wave.surface.hlsl", rhi::ShaderFormat::SpirV, program) ==
          render::SurfaceStatus::Failed);
    CHECK(program == nullptr);
}
