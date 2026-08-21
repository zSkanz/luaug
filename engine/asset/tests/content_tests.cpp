#include "luaug/asset/content.h"
#include "luaug/core/i18n.h"

#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace luaug::asset;
using luaug::core::ContentHash;
using luaug::core::engineCatalog;
using luaug::core::hashText;
using luaug::core::usize;

namespace {

void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

[[nodiscard]] std::vector<std::byte> bytesOf(const std::string& text)
{
    std::vector<std::byte> out(text.size());
    if (!text.empty()) {
        std::memcpy(out.data(), text.data(), text.size());
    }
    return out;
}

[[nodiscard]] std::string textOf(std::span<const std::byte> bytes)
{
    std::string out;
    out.resize(bytes.size());
    if (!bytes.empty()) {
        std::memcpy(out.data(), bytes.data(), bytes.size());
    }
    return out;
}

// A real pack and a real manifest on disk, because a mount that only worked
// against an in-memory fake would be a mount nothing ships.
struct Fixture
{
    std::filesystem::path root;

    Fixture()
    {
        std::error_code ec;
        root = std::filesystem::temp_directory_path(ec) / "luaug-content-tests";
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "loose" / "models", ec);
        REQUIRE(std::filesystem::is_directory(root / "loose" / "models"));
    }

    ~Fixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    static void write(const std::filesystem::path& path, const std::string& text)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // Writes a pack plus its manifest and returns the pack path.
    [[nodiscard]] std::filesystem::path writePack(const std::string& name,
                                                  const std::vector<std::pair<std::string, std::string>>& assets,
                                                  bool breakManifest = false) const
    {
        PackWriter writer;
        std::string manifest = "{\"format\":\"luaug-content-manifest\",\"version\":1,\"assets\":[";
        bool first = true;
        for (const auto& [urn, contents] : assets) {
            const ContentHash hash = writer.addContent(AssetKind::Raw, bytesOf(contents));
            if (!first) {
                manifest += ",";
            }
            first = false;
            const std::string hex = breakManifest ? hashText("nothing in the pack").toHex() : hash.toHex();
            manifest += "{\"urn\":\"" + urn + "\",\"hash\":\"" + hex + "\",\"kind\":\"raw\",\"bytes\":1}";
        }
        manifest += "]}\n";

        const std::filesystem::path packPath = root / (name + ".lpack");
        const std::vector<std::byte> packBytes = writer.build();
        std::error_code ec;
        std::filesystem::create_directories(packPath.parent_path(), ec);
        std::ofstream out(packPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out.write(reinterpret_cast<const char*>(packBytes.data()), static_cast<std::streamsize>(packBytes.size()));
        out.close();

        write(root / (name + ".manifest.json"), manifest);
        return packPath;
    }
};

} // namespace

TEST_CASE("a URN is a name inside the mount, never a way out of it")
{
    CHECK(isValidUrn("asset://models/tree.glb"));
    CHECK(isValidUrn("asset://a"));
    CHECK(urnPath("asset://models/tree.glb") == "models/tree.glb");

    CHECK_FALSE(isValidUrn(""));
    CHECK_FALSE(isValidUrn("asset://"));
    CHECK_FALSE(isValidUrn("models/tree.glb"));
    CHECK_FALSE(isValidUrn("file://models/tree.glb"));
    CHECK_FALSE(isValidUrn("asset:///models/tree.glb"));
    CHECK_FALSE(isValidUrn("asset://models//tree.glb"));

    // The ones that matter: a traversal is refused HERE rather than at the
    // filesystem, because a shipped game resolving against a pack has no
    // filesystem to refuse it.
    CHECK_FALSE(isValidUrn("asset://../secrets"));
    CHECK_FALSE(isValidUrn("asset://models/../../secrets"));
    CHECK_FALSE(isValidUrn("asset://./models/tree.glb"));
    CHECK_FALSE(isValidUrn("asset://models\\tree.glb"));
    CHECK(urnPath("asset://../secrets").empty());
}

TEST_CASE("a directory mount resolves a URN to a file on disk")
{
    const Fixture fixture;
    Fixture::write(fixture.root / "loose" / "models" / "tree.gltf", "{ \"asset\": {} }");

    ContentMounts mounts;
    mounts.mountDirectory(fixture.root / "loose");
    CHECK(mounts.mountCount() == 1);

    const ResolvedContent found = mounts.resolve("asset://models/tree.gltf");
    REQUIRE(found.found());
    CHECK(found.source == ResolvedContent::Source::Loose);
    CHECK(found.path.filename() == "tree.gltf");

    CHECK_FALSE(mounts.contains("asset://models/missing.gltf"));
    CHECK_FALSE(mounts.contains("asset://../loose/models/tree.gltf"));
}

TEST_CASE("a pack mount resolves a URN to bytes")
{
    seedRealCatalog();
    const Fixture fixture;
    const std::filesystem::path pack =
        fixture.writePack("content", {{"asset://a.txt", "the first"}, {"asset://b.txt", "the second"}});

    ContentMounts mounts;
    REQUIRE_FALSE(mounts.mountPack(pack).has_value());

    const ResolvedContent found = mounts.resolve("asset://a.txt");
    REQUIRE(found.found());
    CHECK(found.source == ResolvedContent::Source::Pack);
    CHECK(found.kind == AssetKind::Raw);
    CHECK(textOf(found.bytes) == "the first");
    CHECK(found.hash == hashText("the first"));

    // And by hash, which is what a chunk payload referencing a shared mesh
    // needs.
    CHECK(textOf(mounts.blob(hashText("the second"))) == "the second");
    CHECK(mounts.blob(hashText("never packed")).empty());

    const std::vector<std::string> urns = mounts.packedUrns();
    REQUIRE(urns.size() == 2);
    CHECK(urns[0] == "asset://a.txt");
    CHECK(urns[1] == "asset://b.txt");
}

TEST_CASE("a later mount wins")
{
    seedRealCatalog();
    const Fixture fixture;
    Fixture::write(fixture.root / "loose" / "shared.txt", "from the directory");
    const std::filesystem::path pack = fixture.writePack("over", {{"asset://shared.txt", "from the pack"}});

    ContentMounts mounts;
    mounts.mountDirectory(fixture.root / "loose");
    REQUIRE_FALSE(mounts.mountPack(pack).has_value());

    // Stated in the header because the alternative -- first wins -- makes
    // overriding impossible and looks identical until somebody tries.
    const ResolvedContent found = mounts.resolve("asset://shared.txt");
    REQUIRE(found.found());
    CHECK(found.source == ResolvedContent::Source::Pack);
    CHECK(textOf(found.bytes) == "from the pack");
}

TEST_CASE("a URN in neither mount falls through to the one that has it")
{
    seedRealCatalog();
    const Fixture fixture;
    Fixture::write(fixture.root / "loose" / "only-loose.txt", "loose");
    const std::filesystem::path pack = fixture.writePack("some", {{"asset://only-packed.txt", "packed"}});

    ContentMounts mounts;
    mounts.mountDirectory(fixture.root / "loose");
    REQUIRE_FALSE(mounts.mountPack(pack).has_value());

    CHECK(mounts.resolve("asset://only-packed.txt").source == ResolvedContent::Source::Pack);
    // The pack is searched first and does not have it, so the directory answers
    // -- which is what makes a dev-mode override of one file possible without
    // rebuilding the pack.
    CHECK(mounts.resolve("asset://only-loose.txt").source == ResolvedContent::Source::Loose);
    CHECK_FALSE(mounts.resolve("asset://neither.txt").found());
}

TEST_CASE("a manifest naming a hash the pack lacks is refused at mount")
{
    seedRealCatalog();
    const Fixture fixture;
    const std::filesystem::path pack = fixture.writePack("broken", {{"asset://a.txt", "content"}}, true);

    ContentMounts mounts;
    const auto error = mounts.mountPack(pack);
    // At mount rather than at first use: a game that starts and then cannot
    // find its world is worse than one that says why it will not start.
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.manifest.err.missing_blob") != std::string::npos);
}

TEST_CASE("a missing or malformed manifest is named rather than ignored")
{
    seedRealCatalog();
    const Fixture fixture;
    const std::filesystem::path pack = fixture.writePack("solo", {{"asset://a.txt", "content"}});

    SUBCASE("missing")
    {
        std::error_code ec;
        std::filesystem::remove(fixture.root / "solo.manifest.json", ec);
        ContentMounts mounts;
        const auto error = mounts.mountPack(pack);
        REQUIRE(error.has_value());
        CHECK(error->message.find("asset.manifest.err.open_failed") != std::string::npos);
    }

    SUBCASE("not JSON")
    {
        Fixture::write(fixture.root / "solo.manifest.json", "this is not json at all");
        ContentMounts mounts;
        const auto error = mounts.mountPack(pack);
        REQUIRE(error.has_value());
        CHECK(error->message.find("asset.manifest.err.malformed") != std::string::npos);
    }

    SUBCASE("JSON, but not a content manifest")
    {
        Fixture::write(fixture.root / "solo.manifest.json", "{\"format\":\"something else\"}");
        ContentMounts mounts;
        const auto error = mounts.mountPack(pack);
        REQUIRE(error.has_value());
        CHECK(error->message.find("asset.manifest.err.malformed") != std::string::npos);
    }
}

TEST_CASE("a pack file that is not there is an error rather than an empty mount")
{
    seedRealCatalog();

    ContentMounts mounts;
    const auto error = mounts.mountPack("no-such-file.lpack");
    REQUIRE(error.has_value());
    CHECK(mounts.mountCount() == 0);
    CHECK_FALSE(mounts.contains("asset://anything"));
}

TEST_CASE("clearing drops every mount")
{
    seedRealCatalog();
    const Fixture fixture;
    Fixture::write(fixture.root / "loose" / "a.txt", "loose");
    const std::filesystem::path pack = fixture.writePack("p", {{"asset://b.txt", "packed"}});

    ContentMounts mounts;
    mounts.mountDirectory(fixture.root / "loose");
    REQUIRE_FALSE(mounts.mountPack(pack).has_value());
    CHECK(mounts.mountCount() == 2);

    mounts.clear();
    CHECK(mounts.mountCount() == 0);
    CHECK_FALSE(mounts.contains("asset://a.txt"));
    CHECK_FALSE(mounts.contains("asset://b.txt"));
    CHECK(mounts.packedUrns().empty());
}
