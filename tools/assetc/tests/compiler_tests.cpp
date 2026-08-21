#include "luaug/asset/mesh_format.h"
#include "luaug/asset/pack.h"
#include "luaug/asset/texture.h"
#include "luaug/assetc/compiler.h"
#include "luaug/core/i18n.h"

#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace luaug::assetc;
using luaug::core::engineCatalog;
using luaug::core::usize;

namespace {

void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A content directory built from the glTF fixtures the asset module already
// has, so this case is about the PIPELINE rather than about parsing.
struct Fixture
{
    std::filesystem::path root;

    Fixture()
    {
        std::error_code ec;
        root = std::filesystem::temp_directory_path(ec) / "luaug-assetc-tests";
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "models", ec);
        std::filesystem::create_directories(root / "raw", ec);
        REQUIRE(std::filesystem::is_directory(root / "models"));

        const std::filesystem::path data = LUAUG_ASSET_TEST_DATA;
        copy(data / "quad.gltf", root / "models" / "quad.gltf");
        copy(data / "two_materials.gltf", root / "models" / "two_materials.gltf");
        copy(data / "textured.gltf", root / "models" / "textured.gltf");
        copy(data / "checker.png", root / "models" / "checker.png");
        write(root / "raw" / "notes.txt", "a file the pipeline copies through untouched");
    }

    ~Fixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    static void copy(const std::filesystem::path& from, const std::filesystem::path& to)
    {
        std::error_code ec;
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
        REQUIRE_MESSAGE(!ec, "could not copy " << from.string() << ": " << ec.message());
    }

    static void write(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out << text;
    }

    [[nodiscard]] CompileResult build() const
    {
        CompileOptions options;
        options.inputRoot = root;
        return compile(options);
    }
};

[[nodiscard]] const ManifestEntry* findUrn(const CompileResult& result, const std::string& urn)
{
    for (const ManifestEntry& entry : result.entries) {
        if (entry.urn == urn) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a content directory compiles into a pack and a manifest")
{
    seedRealCatalog();
    const Fixture fixture;

    const CompileResult result = fixture.build();
    REQUIRE_MESSAGE(result.ok, result.diagnostic);

    CHECK(result.meshCount == 3);
    CHECK(result.rawCount == 1);
    // `checker.png` is a texture in its own right AND the one `textured.gltf`
    // samples -- and it is stored once, which is what content addressing buys.
    CHECK(result.textureCount >= 1);

    luaug::asset::Pack pack;
    REQUIRE_FALSE(luaug::asset::Pack::openVerified(result.pack, pack).has_value());

    for (const ManifestEntry& entry : result.entries) {
        CHECK_MESSAGE(pack.contains(entry.hash), "manifest names " << entry.urn << " but the pack lacks it");
    }

    const ManifestEntry* const quad = findUrn(result, "asset://models/quad.gltf");
    REQUIRE(quad != nullptr);
    CHECK(quad->kind == luaug::asset::AssetKind::Mesh);
    CHECK(quad->vertexCount > 0);

    const ManifestEntry* const raw = findUrn(result, "asset://raw/notes.txt");
    REQUIRE(raw != nullptr);
    CHECK(raw->kind == luaug::asset::AssetKind::Raw);
    CHECK(raw->storedBytes == raw->originalBytes);
}

TEST_CASE("a compiled mesh in the pack decodes back into a mesh")
{
    seedRealCatalog();
    const Fixture fixture;

    const CompileResult result = fixture.build();
    REQUIRE_MESSAGE(result.ok, result.diagnostic);

    luaug::asset::Pack pack;
    REQUIRE_FALSE(luaug::asset::Pack::open(result.pack, pack).has_value());

    const ManifestEntry* const entry = findUrn(result, "asset://models/two_materials.gltf");
    REQUIRE(entry != nullptr);

    luaug::asset::CompiledMesh mesh;
    REQUIRE_FALSE(luaug::asset::decodeMesh(pack.blob(entry->hash), mesh).has_value());

    // The end-to-end claim: what the tool wrote is what the engine reads, and
    // there is one implementation of the format rather than two that drift.
    CHECK(mesh.vertices.size() == entry->vertexCount);
    CHECK(mesh.lods.size() == entry->lodCount);
    CHECK(mesh.materials.size() == 2);
    REQUIRE_FALSE(mesh.lods.empty());
    CHECK(mesh.lods[0].submeshes.size() == 2);
}

TEST_CASE("a mesh names its textures by content hash and the pack holds them")
{
    seedRealCatalog();
    const Fixture fixture;

    const CompileResult result = fixture.build();
    REQUIRE_MESSAGE(result.ok, result.diagnostic);

    luaug::asset::Pack pack;
    REQUIRE_FALSE(luaug::asset::Pack::open(result.pack, pack).has_value());

    const ManifestEntry* const entry = findUrn(result, "asset://models/textured.gltf");
    REQUIRE(entry != nullptr);

    luaug::asset::CompiledMesh mesh;
    REQUIRE_FALSE(luaug::asset::decodeMesh(pack.blob(entry->hash), mesh).has_value());
    REQUIRE_FALSE(mesh.images.empty());

    for (const luaug::asset::TextureSlot& slot : mesh.images) {
        REQUIRE(pack.contains(slot.hash));
        luaug::asset::TranscodeOptions options;
        options.forceUncompressed = true;
        luaug::asset::TextureAsset texture;
        REQUIRE_FALSE(luaug::asset::transcodeTexture(pack.blob(slot.hash), options, texture).has_value());
        CHECK(texture.valid());
    }
}

TEST_CASE("the same content directory builds byte-identically twice")
{
    seedRealCatalog();
    const Fixture fixture;

    const CompileResult first = fixture.build();
    const CompileResult second = fixture.build();
    REQUIRE(first.ok);
    REQUIRE(second.ok);

    // The gate item, in the small: "asset build determinism check in CI". Both
    // outputs, because a manifest that matched while the pack differed would
    // be a cache that hands back the wrong bytes under the right name.
    CHECK(first.pack == second.pack);
    CHECK(first.manifest == second.manifest);
}

TEST_CASE("sources are collected in a stable order whatever the filesystem says")
{
    const Fixture fixture;

    std::string diagnostic;
    const std::vector<SourceFile> sources = collectSources(fixture.root, diagnostic);
    CHECK(diagnostic.empty());
    REQUIRE(sources.size() == 5);

    for (usize i = 1; i < sources.size(); ++i) {
        CHECK(sources[i - 1].relative.generic_string() < sources[i].relative.generic_string());
    }
    CHECK(sources.front().kind == SourceKind::Texture);
    CHECK(sources.back().kind == SourceKind::Raw);
}

TEST_CASE("the manifest is JSON, sorted by URN, and says what it produced")
{
    seedRealCatalog();
    const Fixture fixture;

    const CompileResult result = fixture.build();
    REQUIRE(result.ok);

    CHECK(result.manifest.find("\"luaug-content-manifest\"") != std::string::npos);
    CHECK(result.manifest.find("asset://models/quad.gltf") != std::string::npos);
    CHECK(result.manifest.find("\"kind\":\"mesh\"") != std::string::npos);
    CHECK(result.manifest.back() == '\n');

    for (usize i = 1; i < result.entries.size(); ++i) {
        CHECK(result.entries[i - 1].urn < result.entries[i].urn);
    }
}

TEST_CASE("a directory that is not there is a diagnostic rather than an empty build")
{
    CompileOptions options;
    options.inputRoot = "no-such-content-directory";

    const CompileResult result = compile(options);
    // An empty pack reported as success is the failure this project keeps
    // designing against: a build that ran, said nothing, and produced nothing.
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.diagnostic.empty());
}
