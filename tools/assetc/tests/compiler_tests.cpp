#include "luaug/asset/content.h"
#include "luaug/asset/mesh_format.h"
#include "luaug/asset/pack.h"
#include "luaug/asset/texture.h"
#include "luaug/assetc/compiler.h"
#include "luaug/assetc/exotic.h"
#include "luaug/core/i18n.h"
#include "luaug/platform/file.h"

#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <span>
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

// ---------------------------------------------------------------------------
// The exotic importer (roadmap M7: "assimp as the offline-CLI-only importer for
// exotic formats").

TEST_CASE("an OBJ is imported into the same shape a glTF is")
{
    // Written by the test rather than checked in, for the reason the audio test
    // writes its own WAV: a fixture asset is a binary in git nobody can diff.
    // An OBJ is text, which makes the expectations below readable beside it.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "luaug-assetc-exotic";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    // A unit quad: four corners, two triangles, one material.
    const std::string obj = "v 0 0 0\n"
                            "v 1 0 0\n"
                            "v 1 1 0\n"
                            "v 0 1 0\n"
                            "vn 0 0 1\n"
                            "vt 0 0\n"
                            "vt 1 0\n"
                            "vt 1 1\n"
                            "vt 0 1\n"
                            "f 1/1/1 2/2/1 3/3/1\n"
                            "f 1/1/1 3/3/1 4/4/1\n";
    {
        std::ofstream file(root / "quad.obj", std::ios::binary);
        file << obj;
    }

    std::vector<std::byte> bytes;
    REQUIRE(luaug::platform::readFile(root / "quad.obj", bytes));

    luaug::asset::Model model;
    const auto error = luaug::assetc::importExotic(bytes, root, ".obj", model);

#if LUAUG_ASSETC_ASSIMP
    if (error.has_value()) {
        FAIL(error->message);
    }
    REQUIRE_FALSE(error.has_value());

    // Two triangles, and the vertices JOINED: an OBJ stores one vertex per
    // triangle corner, so six without `JoinIdenticalVertices` and four with it.
    CHECK(model.mesh.indices.size() == 6);
    CHECK(model.mesh.vertices.size() == 4);
    REQUIRE(model.mesh.submeshes.size() == 1);
    CHECK(model.mesh.submeshes[0].indexCount == 6);

    // Every submesh names a material, even for a file that declares none: a
    // draw with no material is a draw the renderer cannot make.
    CHECK_FALSE(model.materials.empty());
    CHECK(model.mesh.submeshes[0].material < model.materials.size());

    // NOT metallic. glTF's default is fully metallic, which is right for a file
    // that declares a PBR material and says nothing -- and wrong for one with no
    // PBR model at all, which would make every imported OBJ look like a mirror.
    CHECK(model.materials[0].metallicFactor == doctest::Approx(0.0));

    // Normals and a tangent basis, both generated: the file has one normal and
    // no tangents, and a mesh with neither renders black under a normal map.
    CHECK(model.mesh.vertices[0].normal.z == doctest::Approx(1.0));
    const auto handedness = static_cast<double>(model.mesh.vertices[0].tangent[3]);
    CHECK((handedness == doctest::Approx(1.0) || handedness == doctest::Approx(-1.0)));

    // The bounds are the quad's own, in the model's space.
    CHECK(model.mesh.bounds.min.x == doctest::Approx(0.0));
    CHECK(model.mesh.bounds.max.x == doctest::Approx(1.0));
    CHECK(model.mesh.bounds.max.y == doctest::Approx(1.0));
#else
    // A build with the importer off says which extension it cannot read, rather
    // than failing to link or treating the model as an opaque blob.
    REQUIRE(error.has_value());
    CHECK(error->message.find("assetc.err.exotic_disabled") != std::string::npos);
#endif

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("a file that is not the format its name claims is refused by name")
{
    seedRealCatalog();

    const std::string notAnObj = "this is not a wavefront object at all";
    const auto* const data = reinterpret_cast<const std::byte*>(notAnObj.data());

    luaug::asset::Model model;
    const auto error =
        luaug::assetc::importExotic(std::span<const std::byte>(data, notAnObj.size()), {}, ".fbx", model);
    REQUIRE(error.has_value());
    // Either "could not be imported" or "no geometry", both keyed and both
    // naming the file rather than crashing on it. Which one depends on how far
    // assimp gets before it gives up, and pinning that would be pinning
    // upstream's parser rather than our behaviour.
    CHECK(error->message.find("assetc.err.exotic") != std::string::npos);
}

TEST_CASE("the extensions the importer claims are a closed list")
{
    // Closed rather than "whatever assimp compiled with", because the build
    // chooses which importers exist and a file that classifies as a mesh and
    // then fails to import is a worse error than one that rides through as raw.
    CHECK(luaug::assetc::isExoticMesh(".obj"));
    CHECK(luaug::assetc::isExoticMesh(".fbx"));
    CHECK(luaug::assetc::isExoticMesh(".dae"));
    CHECK_FALSE(luaug::assetc::isExoticMesh(".gltf"));
    CHECK_FALSE(luaug::assetc::isExoticMesh(".glb"));
    CHECK_FALSE(luaug::assetc::isExoticMesh(".png"));
    CHECK_FALSE(luaug::assetc::isExoticMesh(""));
}

// --- Incrementality -----------------------------------------------------------

TEST_CASE("a second build of an unchanged tree compiles nothing")
{
    // **An assertion, not a threshold.** "The second build took 200 ms" is a
    // number that drifts with the machine; "the second build encoded zero
    // textures" is the claim incrementality actually makes, and it either holds
    // or it does not.
    seedRealCatalog();
    const Fixture fixture;

    const std::filesystem::path cache = fixture.root / ".cache";
    CompileOptions options;
    options.inputRoot = fixture.root;
    options.cacheRoot = cache;

    const CompileResult first = compile(options);
    REQUIRE_MESSAGE(first.ok, first.diagnostic);
    CHECK(first.stats.meshesCompiled > 0);
    CHECK(first.stats.texturesEncoded > 0);
    CHECK(first.stats.cacheHits == 0);

    const CompileResult second = compile(options);
    REQUIRE_MESSAGE(second.ok, second.diagnostic);
    CHECK(second.stats.meshesCompiled == 0);
    CHECK(second.stats.texturesEncoded == 0);
    CHECK(second.stats.cacheHits == first.stats.cacheMisses);

    // **And it produced the same build.** A cache that returned faster and
    // differently would be worse than no cache at all -- this is the property
    // the whole content-hash design rests on.
    CHECK(first.pack == second.pack);
    CHECK(first.manifest == second.manifest);
    CHECK(first.meshCount == second.meshCount);
    CHECK(first.textureCount == second.textureCount);
    CHECK(first.rawCount == second.rawCount);
}

TEST_CASE("changing one file recompiles that one and no other")
{
    seedRealCatalog();
    const Fixture fixture;

    const std::filesystem::path cache = fixture.root / ".cache";
    CompileOptions options;
    options.inputRoot = fixture.root;
    options.cacheRoot = cache;

    const CompileResult first = compile(options);
    REQUIRE_MESSAGE(first.ok, first.diagnostic);

    // A raw file is the cheapest thing to change and the easiest to count.
    Fixture::write(fixture.root / "raw" / "notes.txt", "changed");

    const CompileResult second = compile(options);
    REQUIRE_MESSAGE(second.ok, second.diagnostic);
    CHECK(second.stats.cacheMisses == 1);
    CHECK(second.stats.cacheHits == first.stats.cacheMisses - 1);
    // The meshes were not touched, so nothing was recompiled.
    CHECK(second.stats.meshesCompiled == 0);
    CHECK(second.stats.texturesEncoded == 0);
}

TEST_CASE("a cache written for other options is a miss rather than a wrong answer")
{
    // The key carries the pinned options and a rules version. An upstream
    // default change is a diff in this tool, and a cache written before it must
    // not be believed afterwards.
    seedRealCatalog();
    const Fixture fixture;

    const std::filesystem::path cache = fixture.root / ".cache";
    CompileOptions options;
    options.inputRoot = fixture.root;
    options.cacheRoot = cache;
    REQUIRE(compile(options).ok);

    CompileOptions other = options;
    other.mesh.maxLods = options.mesh.maxLods == 1 ? 2u : 1u;
    const CompileResult second = compile(other);
    REQUIRE_MESSAGE(second.ok, second.diagnostic);
    CHECK(second.stats.cacheHits == 0);
    CHECK(second.stats.meshesCompiled > 0);
}

TEST_CASE("a truncated cache file is a miss rather than a crash")
{
    // Written by this machine and still a file on a disk: a build killed halfway
    // through leaves one, and the next run has to survive reading it.
    seedRealCatalog();
    const Fixture fixture;

    const std::filesystem::path cache = fixture.root / ".cache";
    CompileOptions options;
    options.inputRoot = fixture.root;
    options.cacheRoot = cache;
    const CompileResult first = compile(options);
    REQUIRE(first.ok);

    // Truncate every cache file to a handful of bytes.
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(cache, ec), end; it != end; it.increment(ec)) {
        if (!it->is_regular_file())
            continue;
        std::ofstream out(it->path(), std::ios::binary | std::ios::trunc);
        out << "LUA";
    }

    const CompileResult second = compile(options);
    REQUIRE_MESSAGE(second.ok, second.diagnostic);
    CHECK(second.stats.cacheHits == 0);
    // And it built the same thing it did the first time.
    CHECK(first.pack == second.pack);
}

TEST_CASE("no cache root is the behaviour this tool always had")
{
    seedRealCatalog();
    const Fixture fixture;

    CompileOptions options;
    options.inputRoot = fixture.root;

    const CompileResult first = compile(options);
    const CompileResult second = compile(options);
    REQUIRE(first.ok);
    REQUIRE(second.ok);
    CHECK(second.stats.cacheHits == 0);
    CHECK(second.stats.cacheMisses == 0);
    CHECK(second.stats.meshesCompiled == first.stats.meshesCompiled);
    CHECK(first.pack == second.pack);
}

// --- What a loose texture is FOR ----------------------------------------------
//
// A `Material` in a project's content says what each of its maps is: `ColorMap`
// and `EmissiveMap` are colour, `NormalMap` and `MetallicRoughnessMap` are
// numbers (`api/defs/instances.api.luau`). Every image below is the same
// two-by-two PNG, so the ONLY thing that can make two of them different blobs is
// the transfer function they were encoded with.

namespace {

[[nodiscard]] std::string materialNode(const std::string& name, const std::string& maps)
{
    return R"({"class":"Material","name":")" + name + R"(","properties":{)" + maps + "}}";
}

[[nodiscard]] std::string modelNode(const std::string& name, const std::string& child)
{
    return R"({"class":"Model","name":")" + name + R"(","children":[)" + child + "]}";
}

[[nodiscard]] std::string stampedNode(const std::string& name, const std::string& stamp, const std::string& overrides)
{
    return R"({"stamp":")" + stamp + R"(","name":")" + name + R"(","overrides":{)" + overrides + "}}";
}

// A scene whose `Workspace` holds exactly these nodes. Assembled rather than
// written out per case, because the cases differ only in which maps a material
// names and a literal apiece would bury that.
[[nodiscard]] std::string sceneOf(const std::vector<std::string>& children)
{
    std::string text = R"({"format":"luaug-scene","version":1,)"
                       R"("root":{"class":"Workspace","name":"Workspace","children":[)";
    for (usize i = 0; i < children.size(); ++i) {
        if (i > 0)
            text += ',';
        text += children[i];
    }
    text += "]}}";
    return text;
}

struct MaterialFixture
{
    std::filesystem::path root;

    MaterialFixture()
    {
        std::error_code ec;
        root = std::filesystem::temp_directory_path(ec) / "luaug-assetc-material-tests";
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "textures", ec);
        std::filesystem::create_directories(root / "scenes", ec);
        std::filesystem::create_directories(root / "stamps", ec);
        REQUIRE(std::filesystem::is_directory(root / "textures"));

        const std::filesystem::path data = LUAUG_ASSET_TEST_DATA;
        for (const char* name :
             {"both.png", "colour.png", "glow.png", "normal.png", "orm.png", "overridden.png", "unclaimed.png"}) {
            Fixture::copy(data / "checker.png", root / "textures" / name);
        }

        // The stamp names `both.png` as a normal map; the scene names it as a
        // colour map. One image, two uses, and the pack holds it once.
        Fixture::write(root / "stamps" / "post.stamp.json",
                       R"({"format":"luaug-scene","version":1,"root":)" +
                           modelNode("Post", materialNode("Skin", R"("NormalMap":"asset://textures/both.png")")) + "}");
        writeScene({});
    }

    ~MaterialFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    MaterialFixture(const MaterialFixture&) = delete;
    MaterialFixture& operator=(const MaterialFixture&) = delete;

    // `extra` is appended to the standing cast, so a case can author a material
    // that claims an image nothing claimed before.
    void writeScene(const std::vector<std::string>& extra) const
    {
        std::vector<std::string> children{
            materialNode("Brick", R"("ColorMap":"asset://textures/colour.png",)"
                                  R"("NormalMap":"asset://textures/normal.png",)"
                                  R"("MetallicRoughnessMap":"asset://textures/orm.png",)"
                                  R"("EmissiveMap":"asset://textures/glow.png")"),
            // Nested, because a material is an instance and lives wherever the
            // author put it -- not in a list at the top of the file.
            modelNode("Nested", materialNode("Packed", R"("ColorMap":"asset://textures/both.png")")),
            stampedNode("Post", "post", R"("Skin":{"NormalMap":"asset://textures/overridden.png"})"),
        };
        children.insert(children.end(), extra.begin(), extra.end());
        Fixture::write(root / "scenes" / "main.scene.json", sceneOf(children));
    }

    [[nodiscard]] CompileResult build() const
    {
        CompileOptions options;
        options.inputRoot = root;
        return compile(options);
    }
};

// Whether the blob the manifest names for `urn` was encoded through the sRGB
// curve. Read back out of the KTX2's own data format descriptor, so this asks
// the produced bytes rather than the tool's intent.
[[nodiscard]] bool encodedAsColour(const CompileResult& result, const luaug::asset::Pack& pack, const std::string& urn)
{
    const ManifestEntry* const entry = findUrn(result, urn);
    REQUIRE_MESSAGE(entry != nullptr, "no manifest row for " << urn);
    luaug::asset::TranscodeOptions options;
    options.forceUncompressed = true;
    luaug::asset::TextureAsset texture;
    REQUIRE_FALSE(luaug::asset::transcodeTexture(pack.blob(entry->hash), options, texture).has_value());
    return texture.srgb;
}

} // namespace

TEST_CASE("a material decides whether a loose texture is colour or numbers")
{
    seedRealCatalog();
    const MaterialFixture fixture;

    const CompileResult result = fixture.build();
    REQUIRE_MESSAGE(result.ok, result.diagnostic);

    luaug::asset::Pack pack;
    REQUIRE_FALSE(luaug::asset::Pack::openVerified(result.pack, pack).has_value());

    CHECK(encodedAsColour(result, pack, "asset://textures/colour.png"));
    CHECK(encodedAsColour(result, pack, "asset://textures/glow.png"));
    // The defect: a normal map run through the sRGB curve has every value bent
    // by a smooth amount, which reads as bad lighting rather than as a broken
    // texture.
    CHECK_FALSE(encodedAsColour(result, pack, "asset://textures/normal.png"));
    CHECK_FALSE(encodedAsColour(result, pack, "asset://textures/orm.png"));
}

TEST_CASE("an image no material claims is still colour")
{
    // The honest answer for a standalone image: nothing in the project says
    // what it is for, and colour is what most loose textures are.
    seedRealCatalog();
    const MaterialFixture fixture;

    const CompileResult result = fixture.build();
    REQUIRE_MESSAGE(result.ok, result.diagnostic);

    luaug::asset::Pack pack;
    REQUIRE_FALSE(luaug::asset::Pack::openVerified(result.pack, pack).has_value());
    CHECK(encodedAsColour(result, pack, "asset://textures/unclaimed.png"));
}

TEST_CASE("an image used as colour and as numbers is encoded as colour")
{
    // Wrong for one of its two uses and right for the other, and it is what
    // keeps a shared blob shared: splitting it would put the same pixels in the
    // pack twice under two names. The glTF branch already answers this way.
    seedRealCatalog();
    const MaterialFixture fixture;

    const CompileResult result = fixture.build();
    REQUIRE_MESSAGE(result.ok, result.diagnostic);

    luaug::asset::Pack pack;
    REQUIRE_FALSE(luaug::asset::Pack::openVerified(result.pack, pack).has_value());
    CHECK(encodedAsColour(result, pack, "asset://textures/both.png"));
}

TEST_CASE("a map named only in a stamp override is found")
{
    // An override is a property map keyed by a path inside the placed stamp
    // (ADR 0051), and a material map set there is as real as one set in
    // `properties`.
    seedRealCatalog();
    const MaterialFixture fixture;

    const CompileResult result = fixture.build();
    REQUIRE_MESSAGE(result.ok, result.diagnostic);

    luaug::asset::Pack pack;
    REQUIRE_FALSE(luaug::asset::Pack::openVerified(result.pack, pack).has_value());
    CHECK_FALSE(encodedAsColour(result, pack, "asset://textures/overridden.png"));
}

TEST_CASE("the same pixels under two transfer functions are two blobs")
{
    seedRealCatalog();
    const MaterialFixture fixture;

    const CompileResult result = fixture.build();
    REQUIRE_MESSAGE(result.ok, result.diagnostic);

    // Every image in this fixture is the same PNG, so a difference in hash can
    // only be the transfer function -- and the SAMENESS proves the pack still
    // stores one copy of each answer rather than one per file.
    const ManifestEntry* const colour = findUrn(result, "asset://textures/colour.png");
    const ManifestEntry* const glow = findUrn(result, "asset://textures/glow.png");
    const ManifestEntry* const normal = findUrn(result, "asset://textures/normal.png");
    const ManifestEntry* const orm = findUrn(result, "asset://textures/orm.png");
    REQUIRE(colour != nullptr);
    REQUIRE(glow != nullptr);
    REQUIRE(normal != nullptr);
    REQUIRE(orm != nullptr);

    CHECK(colour->hash == glow->hash);
    CHECK(normal->hash == orm->hash);
    CHECK_FALSE(colour->hash == normal->hash);
}

TEST_CASE("a material that starts claiming a texture recompiles it")
{
    // The cache key has to carry the answer. Without it the first build's sRGB
    // blob comes back under the same name once the material that makes the
    // image a normal map is authored -- a cache that is wrong rather than slow,
    // which is the one thing this design says it must never be.
    seedRealCatalog();
    const MaterialFixture fixture;

    CompileOptions options;
    options.inputRoot = fixture.root;
    options.cacheRoot = fixture.root / ".cache";

    const CompileResult first = compile(options);
    REQUIRE_MESSAGE(first.ok, first.diagnostic);

    fixture.writeScene({materialNode("Late", R"("NormalMap":"asset://textures/unclaimed.png")")});

    const CompileResult second = compile(options);
    REQUIRE_MESSAGE(second.ok, second.diagnostic);

    luaug::asset::Pack pack;
    REQUIRE_FALSE(luaug::asset::Pack::openVerified(second.pack, pack).has_value());
    CHECK_FALSE(encodedAsColour(second, pack, "asset://textures/unclaimed.png"));
}

TEST_CASE("a content directory with materials builds byte-identically twice")
{
    // The pre-pass reads the already-sorted source list and merges with
    // colour-wins, so its answer cannot depend on the order it saw things in.
    seedRealCatalog();
    const MaterialFixture fixture;

    const CompileResult first = fixture.build();
    const CompileResult second = fixture.build();
    REQUIRE(first.ok);
    REQUIRE(second.ok);
    CHECK(first.pack == second.pack);
    CHECK(first.manifest == second.manifest);
}

TEST_CASE("two files with identical bytes keep their own names through the cache")
{
    // A cache entry carries the manifest ROW its source produced, and a row is
    // a NAME -- so a key made only of content handed the second of two
    // byte-identical files the first one's row and dropped its own. Every image
    // in this fixture is the same PNG, which is what makes it the reproduction:
    // seven files, one row, six names gone.
    //
    // It happened inside a single build, not only across two: the second file
    // read the cache the first had just written.
    seedRealCatalog();
    const MaterialFixture fixture;

    // The uncached build goes FIRST, because a cache directory inside the
    // content root is skipped only by the build that is told where it is.
    const CompileResult uncached = fixture.build();
    REQUIRE_MESSAGE(uncached.ok, uncached.diagnostic);

    CompileOptions options;
    options.inputRoot = fixture.root;
    options.cacheRoot = fixture.root / ".cache";
    const CompileResult cold = compile(options);
    REQUIRE_MESSAGE(cold.ok, cold.diagnostic);

    const CompileResult warm = compile(options);
    REQUIRE_MESSAGE(warm.ok, warm.diagnostic);
    CHECK(warm.stats.cacheHits > 0);

    // The claim the whole design rests on: a cache is faster and not different.
    CHECK(cold.manifest == uncached.manifest);
    CHECK(warm.manifest == uncached.manifest);
    CHECK(cold.pack == uncached.pack);
    CHECK(warm.pack == uncached.pack);

    for (const char* name :
         {"both.png", "colour.png", "glow.png", "normal.png", "orm.png", "overridden.png", "unclaimed.png"}) {
        const std::string urn = std::string("asset://textures/") + name;
        CHECK_MESSAGE(findUrn(warm, urn) != nullptr, "the cached build lost " << urn);
    }
}

// --- One source, compiled by the same call a full build uses (E9 step 12) ----
//
// **The claim `importOne` exists to make is structural**: the editor's import
// and `assetc` produce the same blobs because they are the same function, not
// because two implementations agree today. So what is checked here is exactly
// that -- the bytes a one-source import produces are the bytes the full build
// produced for that source, and nothing else came with them.

TEST_CASE("importOne compiles one source and nothing else")
{
    seedRealCatalog();
    const MaterialFixture fixture;

    CompileOptions options;
    options.inputRoot = fixture.root;

    const CompileResult whole = compile(options);
    REQUIRE_MESSAGE(whole.ok, whole.diagnostic);
    REQUIRE(whole.entries.size() > 1);

    const CompileResult one = importOne(options, fixture.root / "textures" / "normal.png");
    REQUIRE_MESSAGE(one.ok, one.diagnostic);

    // One row, and it is the row the full build gave that source.
    REQUIRE(one.entries.size() == 1);
    CHECK(one.entries[0].urn == "asset://textures/normal.png");

    bool matched = false;
    for (const ManifestEntry& entry : whole.entries) {
        if (entry.urn != one.entries[0].urn)
            continue;
        matched = true;
        // **The hash is the claim.** Same source, same options, same transfer
        // function decided from the same materials, so the same content-addressed
        // blob -- which is what lets an editor import and a command-line build
        // share one cache and one pack.
        CHECK(entry.hash == one.entries[0].hash);
        CHECK(entry.kind == one.entries[0].kind);
    }
    CHECK(matched);
}

TEST_CASE("importOne still reads the whole tree to decide what a texture is for")
{
    // The narrowing is applied AFTER the walk on purpose. A texture's transfer
    // function comes from the materials that name it, and those live in scenes
    // and stamps this import is not compiling -- so an import that only looked
    // at its own file would encode a normal map as colour, which is the exact
    // defect the sRGB work closed.
    seedRealCatalog();
    const MaterialFixture fixture;

    CompileOptions options;
    options.inputRoot = fixture.root;

    const CompileResult colour = importOne(options, fixture.root / "textures" / "colour.png");
    const CompileResult normal = importOne(options, fixture.root / "textures" / "normal.png");
    REQUIRE_MESSAGE(colour.ok, colour.diagnostic);
    REQUIRE_MESSAGE(normal.ok, normal.diagnostic);
    REQUIRE(colour.entries.size() == 1);
    REQUIRE(normal.entries.size() == 1);

    // The same pixels through two transfer functions are two different blobs.
    // Equal hashes here would mean the material sweep did not run.
    CHECK(colour.entries[0].hash != normal.entries[0].hash);
}

TEST_CASE("importOne on a path outside the input root compiles nothing")
{
    // A URN is the path relative to the root, so a source outside it has no
    // name. Refused by producing nothing rather than by inventing one.
    seedRealCatalog();
    const MaterialFixture fixture;

    CompileOptions options;
    options.inputRoot = fixture.root;

    const CompileResult outside = importOne(options, std::filesystem::path(LUAUG_ASSET_TEST_DATA) / "checker.png");
    CHECK(outside.ok);
    CHECK(outside.entries.empty());
}

// --- The object store: what an editor writes instead of a pack (E9 step 12) --
//
// `ContentMounts::mountObjects` has been able to READ one since E9 opened and
// nothing could write one. **A store rather than a pack because an editor
// imports one file at a time**: a `.lpack` is one file holding everything, so
// adding to it means rewriting it, which is the wrong shape for a tool where
// somebody drops a model into a folder and expects the other forty to still be
// there.

TEST_CASE("a store an import wrote is a store the engine can mount")
{
    seedRealCatalog();
    const MaterialFixture fixture;

    CompileOptions options;
    options.inputRoot = fixture.root;
    const std::filesystem::path objects = fixture.root / ".luaug" / "objects";
    const std::filesystem::path index = fixture.root / ".luaug" / "index.json";

    const CompileResult one = importOne(options, fixture.root / "textures" / "normal.png");
    REQUIRE_MESSAGE(one.ok, one.diagnostic);
    REQUIRE(one.entries.size() == 1);
    REQUIRE_FALSE(writeObjectStore(one, objects, index).has_value());

    // The round trip, and it is the assertion that matters: what the writer
    // produced is what the reader accepts, rather than two sides that agree
    // about a format on paper.
    luaug::asset::ContentMounts mounts;
    REQUIRE_FALSE(mounts.mountObjects(objects, index).has_value());

    const luaug::asset::ResolvedContent found = mounts.resolve("asset://textures/normal.png");
    CHECK(found.found());
    CHECK(found.hash == one.entries[0].hash);
    CHECK_FALSE(found.bytes.empty());
}

TEST_CASE("a second import adds to the store rather than replacing it")
{
    // The whole reason a store exists. An import that replaced the index would
    // make importing a second model delete the first, which is the behaviour a
    // pack would have forced.
    seedRealCatalog();
    const MaterialFixture fixture;

    CompileOptions options;
    options.inputRoot = fixture.root;
    const std::filesystem::path objects = fixture.root / ".luaug" / "objects";
    const std::filesystem::path index = fixture.root / ".luaug" / "index.json";

    const CompileResult first = importOne(options, fixture.root / "textures" / "normal.png");
    const CompileResult second = importOne(options, fixture.root / "textures" / "colour.png");
    REQUIRE_MESSAGE(first.ok, first.diagnostic);
    REQUIRE_MESSAGE(second.ok, second.diagnostic);
    REQUIRE_FALSE(writeObjectStore(first, objects, index).has_value());
    REQUIRE_FALSE(writeObjectStore(second, objects, index).has_value());

    luaug::asset::ContentMounts mounts;
    REQUIRE_FALSE(mounts.mountObjects(objects, index).has_value());
    CHECK(mounts.contains("asset://textures/normal.png"));
    CHECK(mounts.contains("asset://textures/colour.png"));
}

TEST_CASE("re-importing one source replaces its row rather than adding a second")
{
    // A URN names one blob. Two rows for it would make which one wins depend on
    // the order the index happened to be read in, which is the kind of answer
    // that is right until somebody adds an import.
    seedRealCatalog();
    const MaterialFixture fixture;

    CompileOptions options;
    options.inputRoot = fixture.root;
    const std::filesystem::path objects = fixture.root / ".luaug" / "objects";
    const std::filesystem::path index = fixture.root / ".luaug" / "index.json";

    const CompileResult one = importOne(options, fixture.root / "textures" / "normal.png");
    REQUIRE_MESSAGE(one.ok, one.diagnostic);
    REQUIRE_FALSE(writeObjectStore(one, objects, index).has_value());
    REQUIRE_FALSE(writeObjectStore(one, objects, index).has_value());

    std::string text;
    REQUIRE(luaug::platform::readTextFile(index, text));
    // Counted in the text rather than through the reader, because the reader
    // would happily accept a duplicate and quietly keep one of them.
    usize occurrences = 0;
    for (usize at = text.find("asset://textures/normal.png"); at != std::string::npos;
         at = text.find("asset://textures/normal.png", at + 1)) {
        ++occurrences;
    }
    CHECK(occurrences == 1);
}

TEST_CASE("the store the writer produces is a pure function of what is in it")
{
    // Written twice from two imports in the opposite order. The index is keyed
    // and sorted, so the bytes must not remember which import happened first --
    // the same property the pack has and for the same reason.
    seedRealCatalog();
    const MaterialFixture fixture;

    CompileOptions options;
    options.inputRoot = fixture.root;
    const CompileResult a = importOne(options, fixture.root / "textures" / "normal.png");
    const CompileResult b = importOne(options, fixture.root / "textures" / "colour.png");
    REQUIRE_MESSAGE(a.ok, a.diagnostic);
    REQUIRE_MESSAGE(b.ok, b.diagnostic);

    const auto build = [&](const CompileResult& first, const CompileResult& second, const char* label) {
        const std::filesystem::path objects = fixture.root / ".luaug" / label / "objects";
        const std::filesystem::path index = fixture.root / ".luaug" / label / "index.json";
        REQUIRE_FALSE(writeObjectStore(first, objects, index).has_value());
        REQUIRE_FALSE(writeObjectStore(second, objects, index).has_value());
        std::string text;
        REQUIRE(luaug::platform::readTextFile(index, text));
        return text;
    };

    CHECK(build(a, b, "forward") == build(b, a, "backward"));
}
