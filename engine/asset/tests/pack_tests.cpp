#include "luaug/asset/pack.h"
#include "luaug/core/content_hash.h"
#include "luaug/core/i18n.h"
#include "luaug/core/random.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace luaug::asset;
using luaug::core::ContentHash;
using luaug::core::engineCatalog;
using luaug::core::hashText;
using luaug::core::u32;
using luaug::core::u64;
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

// A pack with a handful of blobs of different kinds and sizes, which is what
// the corruption cases below chew on.
[[nodiscard]] std::vector<std::byte> samplePack()
{
    PackWriter writer;
    writer.addContent(AssetKind::Mesh, bytesOf("a mesh, or near enough for a table of contents"));
    writer.addContent(AssetKind::Texture, bytesOf(std::string(4096, 'T')));
    writer.addContent(AssetKind::Prefab, bytesOf("return prefab.define 'Tree' {}"));
    writer.addContent(AssetKind::Raw, bytesOf(""));
    writer.addContent(AssetKind::Chunk, bytesOf(std::string(517, '\x7f')));
    return writer.build();
}

} // namespace

TEST_CASE("a pack round-trips every blob it was given")
{
    seedRealCatalog();

    PackWriter writer;
    const ContentHash mesh = writer.addContent(AssetKind::Mesh, bytesOf("vertices and indices"));
    const ContentHash texture = writer.addContent(AssetKind::Texture, bytesOf(std::string(2000, 'x')));
    const ContentHash empty = writer.addContent(AssetKind::Raw, bytesOf(""));
    CHECK(writer.count() == 3);

    Pack pack;
    REQUIRE_FALSE(Pack::openVerified(writer.build(), pack).has_value());

    CHECK(pack.count() == 3);
    CHECK(textOf(pack.blob(mesh)) == "vertices and indices");
    CHECK(textOf(pack.blob(texture)) == std::string(2000, 'x'));
    CHECK(pack.blob(empty).empty());
    CHECK(pack.contains(empty));

    const PackEntry* const entry = pack.find(mesh);
    REQUIRE(entry != nullptr);
    CHECK(entry->kind == AssetKind::Mesh);
    CHECK(entry->storedSize == 20);

    // An empty blob is a blob: `contains` says so even though `blob` is empty,
    // which is what keeps "the asset is missing" and "the asset has no bytes"
    // different answers.
    CHECK_FALSE(pack.contains(hashText("never added")));
    CHECK(pack.blob(hashText("never added")).empty());
}

TEST_CASE("the same content produces the same pack, whatever order it arrived in")
{
    PackWriter forwards;
    forwards.addContent(AssetKind::Mesh, bytesOf("one"));
    forwards.addContent(AssetKind::Texture, bytesOf("two"));
    forwards.addContent(AssetKind::Prefab, bytesOf("three"));

    PackWriter backwards;
    backwards.addContent(AssetKind::Prefab, bytesOf("three"));
    backwards.addContent(AssetKind::Texture, bytesOf("two"));
    backwards.addContent(AssetKind::Mesh, bytesOf("one"));

    // Byte-identical, and this is the property the CI asset-determinism check
    // rests on: same inputs, same hashes, same file.
    CHECK(forwards.build() == backwards.build());
}

TEST_CASE("identical content is stored once")
{
    PackWriter writer;
    const ContentHash first = writer.addContent(AssetKind::Mesh, bytesOf("a shared tree trunk"));
    const ContentHash second = writer.addContent(AssetKind::Mesh, bytesOf("a shared tree trunk"));
    CHECK(first == second);
    CHECK(writer.count() == 1);

    // Dedupe across chunks for free is the whole reason the store is addressed
    // by content rather than by path (architecture.md §10).
    Pack pack;
    REQUIRE_FALSE(Pack::open(writer.build(), pack).has_value());
    CHECK(pack.count() == 1);
}

TEST_CASE("an empty pack is valid and holds nothing")
{
    PackWriter writer;
    Pack pack;
    REQUIRE_FALSE(Pack::openVerified(writer.build(), pack).has_value());
    CHECK(pack.count() == 0);
    CHECK(pack.entries().empty());
}

TEST_CASE("entries come back sorted by hash")
{
    Pack pack;
    REQUIRE_FALSE(Pack::open(samplePack(), pack).has_value());

    const std::span<const PackEntry> entries = pack.entries();
    REQUIRE(entries.size() == 5);
    for (usize i = 1; i < entries.size(); ++i) {
        CHECK(entries[i - 1].hash < entries[i].hash);
    }
}

TEST_CASE("a file that is not a pack is refused by name")
{
    seedRealCatalog();

    Pack pack;
    const auto error = Pack::open(bytesOf(std::string(200, 'z')), pack);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.pack.err.magic") != std::string::npos);
    CHECK(pack.count() == 0);
}

TEST_CASE("a truncated pack is an error at every length")
{
    seedRealCatalog();
    const std::vector<std::byte> good = samplePack();

    // Every prefix of a valid pack, which is what a half-finished download and
    // a half-written build both look like. The requirement is a structured
    // error and no crash -- at no length may a header be believed about bytes
    // that are not there.
    for (usize length = 0; length < good.size(); ++length) {
        std::vector<std::byte> truncated(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
        Pack pack;
        const auto error = Pack::openVerified(std::move(truncated), pack);
        REQUIRE_MESSAGE(error.has_value(), "a prefix of length " << length << " was accepted");
        CHECK(pack.count() == 0);
    }
}

TEST_CASE("a corrupted pack is an error and never a crash")
{
    seedRealCatalog();
    const std::vector<std::byte> good = samplePack();

    // A seeded PCG rather than an unseeded one, so a failure here is a failure
    // anybody can reproduce by running the suite again (R10's instinct applied
    // to a test).
    luaug::core::Pcg32 random(0x5eed1234u, 0xfeed5678u);

    int accepted = 0;
    for (int attempt = 0; attempt < 4000; ++attempt) {
        std::vector<std::byte> corrupted = good;
        const u32 flips = 1 + (random.nextU32() % 4);
        for (u32 i = 0; i < flips; ++i) {
            const u32 at = random.nextU32() % static_cast<u32>(corrupted.size());
            const auto mask = static_cast<std::byte>(1u << (random.nextU32() % 8));
            corrupted[at] ^= mask;
        }

        Pack pack;
        const auto error = Pack::openVerified(std::move(corrupted), pack);
        if (!error.has_value()) {
            accepted += 1;
            continue;
        }
        // Every refusal names a check rather than being a bare failure.
        CHECK(error->message.find("asset.pack.err.") != std::string::npos);
    }

    // Zero, and getting to zero changed the FORMAT rather than this number.
    // The first run of this case accepted one flip in four thousand: a bit in
    // an entry's `kind`, which turns a mesh into a prefab and passes every
    // structural check because nothing else in the file said what that entry
    // should have been. The header now carries a hash of the table of contents
    // and every open checks it, so the three regions of a pack -- header, TOC,
    // blobs -- are each covered by something.
    CHECK(accepted == 0);
}

TEST_CASE("an entry pointing outside the pack is refused on its own merits")
{
    seedRealCatalog();
    std::vector<std::byte> pack = samplePack();

    // The TOC starts where the header says it does; the first entry's offset
    // field is 16 bytes into it. Pushed past the end of the file, which is the
    // hand-written attack a fuzzer reaches only by luck.
    u64 tocOffset = 0;
    for (usize i = 0; i < 8; ++i) {
        tocOffset |= static_cast<u64>(static_cast<unsigned char>(pack[16 + i])) << (i * 8);
    }
    for (usize i = 0; i < 8; ++i) {
        pack[static_cast<usize>(tocOffset) + 16 + i] = static_cast<std::byte>(0xFFu);
    }

    // The TOC hash is REPAIRED after the corruption, deliberately: otherwise
    // this case would only ever reach the hash check, and the bounds check --
    // the one thing standing between a malicious pack and a read past the end
    // of the buffer -- would be covered by nothing at all.
    const std::array<std::byte, 16> repaired = luaug::core::toBytes(luaug::core::hashBytes(
        std::span<const std::byte>(pack.data() + tocOffset, pack.size() - static_cast<usize>(tocOffset))));
    std::memcpy(pack.data() + 32, repaired.data(), repaired.size());

    Pack opened;
    const auto error = Pack::open(std::move(pack), opened);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.pack.err.entry") != std::string::npos);
}

TEST_CASE("a table of contents that does not match its header hash is refused")
{
    seedRealCatalog();
    std::vector<std::byte> pack = samplePack();

    u64 tocOffset = 0;
    for (usize i = 0; i < 8; ++i) {
        tocOffset |= static_cast<u64>(static_cast<unsigned char>(pack[16 + i])) << (i * 8);
    }
    // The `kind` field of the first entry: 40 bytes into the record. A mesh
    // becomes a prefab, every structural check still passes, and only the hash
    // notices. This is the flip the fuzz case found.
    pack[static_cast<usize>(tocOffset) + 40] ^= std::byte{0x01};

    Pack opened;
    const auto error = Pack::open(std::move(pack), opened);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.pack.err.toc_hash") != std::string::npos);
}

TEST_CASE("a blob whose bytes no longer hash to its name is refused by a verifying open")
{
    seedRealCatalog();
    std::vector<std::byte> pack = samplePack();

    // A byte inside the payload region, which the structural checks cannot see
    // and only rehashing can.
    pack[PackHeaderBytes + 4] ^= std::byte{0x40};

    Pack structural;
    // The structure is still perfectly consistent, so a plain open accepts it.
    CHECK_FALSE(Pack::open(pack, structural).has_value());

    Pack verified;
    const auto error = Pack::openVerified(std::move(pack), verified);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.pack.err.hash_mismatch") != std::string::npos);
}

TEST_CASE("a pack from a future format version says so")
{
    seedRealCatalog();
    std::vector<std::byte> pack = samplePack();
    pack[4] = static_cast<std::byte>(PackFormatVersion + 1);

    Pack opened;
    const auto error = Pack::open(std::move(pack), opened);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.pack.err.version") != std::string::npos);
}
