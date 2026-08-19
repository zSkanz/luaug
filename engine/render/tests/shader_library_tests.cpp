#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string_view>

#include "luaug/core/text_key.h"
#include "luaug/render/shader_library.h"

using luaug::render::ShaderLibrary;
using luaug::rhi::ShaderFormat;

namespace
{

// A throwaway content directory. The library's contract is about what it does
// with files on disk, so testing it against a fixture in memory would test
// something else.
struct ContentFixture
{
    explicit ContentFixture(std::string_view name)
        : root(std::filesystem::temp_directory_path() / ("luaug-shaderlib-" + std::string(name)))
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "shaders" / "spirv", ec);
        std::filesystem::create_directories(root / "shaders" / "reflect", ec);
    }

    ~ContentFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    ContentFixture(const ContentFixture&) = delete;
    ContentFixture& operator=(const ContentFixture&) = delete;

    void write(const std::filesystem::path& relative, std::string_view text) const
    {
        std::ofstream file(root / relative, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    std::filesystem::path root;
};

constexpr std::string_view kManifest = R"({
  "version": 1,
  "shaders": [
    {
      "name": "debug_line",
      "stage": "vertex",
      "entrypoint": "VertexMain",
      "reflect": "reflect/debug_line.vertex.json",
      "formats": { "spirv": "spirv/debug_line.vertex.spv", "dxil": "dxil/debug_line.vertex.dxil" }
    }
  ]
})";

constexpr std::string_view kReflect = R"({ "samplers": 2, "uniform_buffers": 1 })";

} // namespace

TEST_CASE("a manifest and its reflection load together")
{
    const ContentFixture content("ok");
    content.write("shaders/manifest.json", kManifest);
    content.write("shaders/reflect/debug_line.vertex.json", kReflect);
    content.write("shaders/spirv/debug_line.vertex.spv", "not really spirv");

    ShaderLibrary library;
    const auto error = library.load(content.root, ShaderFormat::SpirV);

    REQUIRE_MESSAGE(!error.has_value(), (error ? error->message : std::string{}));
    CHECK_FALSE(library.empty());
    CHECK(library.format() == ShaderFormat::SpirV);
}

TEST_CASE("a device with no shader format is refused up front")
{
    // Otherwise every create() would fail individually, each for a reason that
    // has nothing to do with the shader being asked for.
    const ContentFixture content("noformat");
    content.write("shaders/manifest.json", kManifest);

    ShaderLibrary library;
    CHECK(library.load(content.root, ShaderFormat::Unknown).has_value());
}

TEST_CASE("a missing manifest is reported by its key")
{
    const ContentFixture content("nomanifest");

    ShaderLibrary library;
    const auto error = library.load(content.root, ShaderFormat::SpirV);

    REQUIRE(error.has_value());
    // The key, not the prose. ADR 0019 makes the TextKey the identity of an
    // engine error precisely so a test does not break when someone rewords a
    // sentence -- and a first draft of this file asserted on the English text,
    // which failed the moment it ran without a catalog loaded.
    CHECK(error->key.hash == LUAUG_TR("render.err.shader_manifest_missing").hash);
}

TEST_CASE("a missing reflection sidecar fails loudly rather than defaulting to zero")
{
    // Zero would link and then bind nothing: SDL_GPU rejects a shader whose
    // declared counts disagree with its bindings, and only the shader source
    // knows them. Guessing here is exactly the failure the sidecar prevents.
    const ContentFixture content("noreflect");
    content.write("shaders/manifest.json", kManifest);
    content.write("shaders/spirv/debug_line.vertex.spv", "blob");

    ShaderLibrary library;
    const auto error = library.load(content.root, ShaderFormat::SpirV);

    REQUIRE(error.has_value());
    CHECK(error->key.hash == LUAUG_TR("render.err.shader_reflect_missing").hash);
}

TEST_CASE("a format nothing was compiled for is reported, not silently empty")
{
    const ContentFixture content("noformatentries");
    content.write("shaders/manifest.json", kManifest);
    content.write("shaders/reflect/debug_line.vertex.json", kReflect);

    // The manifest lists spirv and dxil, so msl matches nothing.
    ShaderLibrary library;
    const auto error = library.load(content.root, ShaderFormat::Msl);

    REQUIRE(error.has_value());
    CHECK(library.empty());
}

TEST_CASE("a malformed manifest reports the parse diagnostic")
{
    const ContentFixture content("badjson");
    content.write("shaders/manifest.json", R"({ "shaders": [ )");

    ShaderLibrary library;
    const auto error = library.load(content.root, ShaderFormat::SpirV);

    REQUIRE(error.has_value());
    // The diagnostic carries the byte offset the reader produced; losing it
    // would leave "invalid JSON" and nothing to act on.
    CHECK_FALSE(error->detail.empty());
}
