// The editor's surface compiler (ADR 0091) against the real `shadercross` and
// the engine's real headers: a surface compiles into every variant, a broken
// one fails with its line, and a saved fix is picked up and compiled again.
#include "luaug/app/surface_compiler.h"
#include "luaug/core/i18n.h"
#include "luaug/platform/file.h"

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
