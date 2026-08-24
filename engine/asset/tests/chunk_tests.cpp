#include "luaug/asset/chunk.h"
#include "luaug/core/i18n.h"

#include <cstddef>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace luaug::asset;
using luaug::core::engineCatalog;
using luaug::core::usize;

namespace {

void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

[[nodiscard]] Chunk sampleChunk()
{
    Chunk chunk;
    chunk.id = ChunkId{3, -2, 0};
    chunk.bounds = chunkBounds(chunk.id, DefaultChunkSize);
    chunk.bounds.min.y = -10.0;
    chunk.bounds.max.y = 40.0;
    chunk.strings = {"Ground", "Rock", "asset://models/rock.glb"};

    ChunkInstance ground;
    ground.kind = ChunkInstance::Kind::Part;
    ground.cframe.position = luaug::core::DVec3{768.0, 0.0, -512.0};
    ground.size = luaug::core::Vec3{256.0f, 1.0f, 256.0f};
    ground.color = luaug::core::Color3{0.3f, 0.5f, 0.25f};
    ground.name = 0;
    chunk.instances.push_back(ground);

    ChunkInstance rock;
    rock.kind = ChunkInstance::Kind::MeshPart;
    rock.cframe.position = luaug::core::DVec3{800.5, 1.25, -500.75};
    rock.cframe.rotation.m[0][0] = 0.5f;
    rock.size = luaug::core::Vec3{2.0f, 3.0f, 2.0f};
    rock.transparency = 0.25f;
    rock.anchored = false;
    rock.name = 1;
    rock.meshContent = 2;
    chunk.instances.push_back(rock);

    return chunk;
}

} // namespace

TEST_CASE("a chunk round-trips through its format")
{
    seedRealCatalog();

    const Chunk source = sampleChunk();
    Chunk decoded;
    REQUIRE_FALSE(decodeChunk(encodeChunk(source), decoded).has_value());

    CHECK(decoded.id == source.id);
    CHECK(decoded.bounds == source.bounds);
    REQUIRE(decoded.instances.size() == 2);
    CHECK(decoded.strings == source.strings);

    CHECK(decoded.instances[0].kind == ChunkInstance::Kind::Part);
    CHECK(decoded.instances[0].cframe.position == source.instances[0].cframe.position);
    CHECK(decoded.instances[0].size == source.instances[0].size);
    CHECK(decoded.instances[0].color == source.instances[0].color);
    CHECK(decoded.stringAt(decoded.instances[0].name) == "Ground");

    CHECK(decoded.instances[1].kind == ChunkInstance::Kind::MeshPart);
    CHECK(decoded.instances[1].anchored == false);
    CHECK(decoded.instances[1].transparency == 0.25f);
    CHECK(decoded.instances[1].cframe.rotation.m[0][0] == 0.5f);
    CHECK(decoded.stringAt(decoded.instances[1].meshContent) == "asset://models/rock.glb");

    // A position is f64 all the way through: a chunk ten thousand kilometres
    // out places its instances to the millimetre, which is the whole reason the
    // record does not store a float.
    Chunk far = source;
    far.instances[0].cframe.position = luaug::core::DVec3{1.0e7 + 0.125, 3.0, -1.0e7};
    Chunk farBack;
    REQUIRE_FALSE(decodeChunk(encodeChunk(far), farBack).has_value());
    CHECK(farBack.instances[0].cframe.position.x == 1.0e7 + 0.125);
}

TEST_CASE("encoding the same chunk twice produces the same bytes")
{
    const Chunk source = sampleChunk();
    CHECK(encodeChunk(source) == encodeChunk(source));
}

TEST_CASE("a corrupted chunk is an error and never a crash")
{
    seedRealCatalog();
    const std::vector<std::byte> good = encodeChunk(sampleChunk());

    for (usize length = 0; length < good.size(); ++length) {
        std::vector<std::byte> truncated(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
        Chunk decoded;
        CHECK(decodeChunk(truncated, decoded).has_value());
    }

    // Every single-bit flip in the header, which is where a believed count
    // would come from. The requirement is an answer rather than a particular
    // one -- a flipped bit in a coordinate is still a valid chunk.
    for (usize at = 0; at < 80 && at < good.size(); ++at) {
        for (int bit = 0; bit < 8; ++bit) {
            std::vector<std::byte> corrupted = good;
            corrupted[at] ^= static_cast<std::byte>(1u << bit);
            Chunk decoded;
            (void)decodeChunk(corrupted, decoded);
        }
    }
}

TEST_CASE("a chunk that is not one is refused by name")
{
    seedRealCatalog();

    std::vector<std::byte> noise(256, std::byte{0x11});
    Chunk decoded;
    const auto error = decodeChunk(noise, decoded);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.chunk.err.magic") != std::string::npos);
}

TEST_CASE("a string index that names nothing is refused")
{
    seedRealCatalog();

    Chunk broken = sampleChunk();
    broken.instances[1].meshContent = 99;
    Chunk decoded;
    const auto error = decodeChunk(encodeChunk(broken), decoded);
    // It would otherwise surface as an invisible part rather than as an error,
    // which is the failure this project keeps designing against.
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.chunk.err.malformed") != std::string::npos);
}

TEST_CASE("the grid puts a position in the cell that contains it")
{
    CHECK(chunkIdAt(luaug::core::DVec3{0.0, 0.0, 0.0}, 256.0f) == ChunkId{0, 0, 0});
    CHECK(chunkIdAt(luaug::core::DVec3{255.9, 0.0, 1.0}, 256.0f) == ChunkId{0, 0, 0});
    CHECK(chunkIdAt(luaug::core::DVec3{256.0, 0.0, 0.0}, 256.0f) == ChunkId{1, 0, 0});

    // The negative side, which is where a cast would have been wrong: truncation
    // toward zero puts -0.5 and +0.5 in the same cell, and that seam runs down
    // the middle of every world.
    CHECK(chunkIdAt(luaug::core::DVec3{-0.5, 0.0, 0.0}, 256.0f) == ChunkId{-1, 0, 0});
    CHECK(chunkIdAt(luaug::core::DVec3{-256.0, 0.0, -256.0}, 256.0f) == ChunkId{-1, -1, 0});
    CHECK(chunkIdAt(luaug::core::DVec3{-256.1, 0.0, 0.0}, 256.0f) == ChunkId{-2, 0, 0});

    const luaug::core::DAABB bounds = chunkBounds(ChunkId{2, -1, 0}, 256.0f);
    CHECK(bounds.min.x == 512.0);
    CHECK(bounds.max.x == 768.0);
    CHECK(bounds.min.z == -256.0);
    CHECK(bounds.max.z == 0.0);
}

TEST_CASE("a chunk index round-trips and comes back sorted")
{
    seedRealCatalog();

    ChunkIndex index;
    index.chunkSize = 128.0f;
    for (const ChunkId id : {ChunkId{2, 2, 0}, ChunkId{-1, 0, 0}, ChunkId{0, 0, 0}}) {
        ChunkIndexEntry entry;
        entry.id = id;
        entry.bounds = chunkBounds(id, index.chunkSize);
        entry.bounds.min.y = -5.0;
        entry.bounds.max.y = 25.0;
        entry.urn = "asset://world/chunk.lchunk";
        entry.instanceCount = 7;
        entry.bytes = 512;
        index.chunks.push_back(entry);
    }

    ChunkIndex parsed;
    REQUIRE_FALSE(readChunkIndex(writeChunkIndex(index), parsed).has_value());

    CHECK(parsed.chunkSize == 128.0f);
    REQUIRE(parsed.chunks.size() == 3);
    // Sorted, which is what makes `find` a binary search and the materialisation
    // order a property of the world rather than of the file.
    CHECK(parsed.chunks[0].id == ChunkId{-1, 0, 0});
    CHECK(parsed.chunks[1].id == ChunkId{0, 0, 0});
    CHECK(parsed.chunks[2].id == ChunkId{2, 2, 0});

    CHECK(parsed.find(ChunkId{2, 2, 0}) != nullptr);
    CHECK(parsed.find(ChunkId{9, 9, 0}) == nullptr);
    CHECK(parsed.find(ChunkId{0, 0, 0})->instanceCount == 7);
    CHECK(parsed.find(ChunkId{0, 0, 0})->bounds.max.y == 25.0);
}

TEST_CASE("a malformed chunk index is refused rather than half-read")
{
    seedRealCatalog();

    ChunkIndex parsed;
    CHECK(readChunkIndex("not json", parsed).has_value());
    CHECK(readChunkIndex("{\"format\":\"something-else\"}", parsed).has_value());
    CHECK(readChunkIndex("{\"format\":\"luaug-chunk-index\",\"chunkSize\":0,\"chunks\":[]}", parsed).has_value());

    const auto error = readChunkIndex(
        "{\"format\":\"luaug-chunk-index\",\"chunkSize\":256,\"chunks\":[{\"x\":0,\"z\":0,\"urn\":\"\"}]}", parsed);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.chunk.err.index_malformed") != std::string::npos);
}

TEST_CASE("a group, its members and their tags survive the format")
{
    seedRealCatalog();

    // The one file-format change E5 makes (ADR 0053), and the two additions it
    // needed to be honest: a record now carries the rest of what a `BasePart`
    // is, and it carries the tags a script finds it by.
    Chunk chunk;
    chunk.id = ChunkId{1, 1, 1};
    chunk.bounds = chunkBounds(chunk.id, DefaultChunkSize);
    chunk.strings = {"Gatehouse", "Pier", "Landmark", "Glass"};
    chunk.groups.push_back(ChunkGroup{0});
    chunk.tagRefs = {2};

    ChunkInstance pier;
    pier.name = 1;
    pier.group = 0;
    pier.firstTag = 0;
    pier.tagCount = 1;
    pier.canCollide = false;
    pier.canQuery = false;
    pier.friction = 0.05f;
    pier.restitution = 0.9f;
    pier.density = 3.5f;
    pier.collisionGroup = 3;
    pier.collisionFidelity = 2;
    chunk.instances.push_back(pier);

    const std::vector<std::byte> bytes = encodeChunk(chunk);
    Chunk decoded;
    REQUIRE(!decodeChunk(bytes, decoded).has_value());

    REQUIRE(decoded.groups.size() == 1);
    CHECK(decoded.stringAt(decoded.groups[0].name) == "Gatehouse");
    REQUIRE(decoded.instances.size() == 1);
    const ChunkInstance& back = decoded.instances[0];
    CHECK(back.group == 0);
    CHECK(back.tagCount == 1);
    CHECK(decoded.stringAt(decoded.tagRefs[back.firstTag]) == "Landmark");
    CHECK(back.canCollide == false);
    CHECK(back.canQuery == false);
    // Compared EXACTLY rather than approximately, because the format's job is
    // to give back the same bits: a tolerance here would pass a writer that
    // truncated a float on its way to disk.
    CHECK(back.friction == 0.05f);
    CHECK(back.restitution == 0.9f);
    CHECK(back.density == 3.5f);
    CHECK(decoded.stringAt(back.collisionGroup) == "Glass");
    CHECK(back.collisionFidelity == 2);
}

TEST_CASE("a group index that names nothing is refused")
{
    seedRealCatalog();

    // The same rule the string indices have, one level along: an index that
    // names no group would surface as a part parented to nothing rather than as
    // an error.
    Chunk chunk;
    chunk.id = ChunkId{0, 0, 0};
    chunk.bounds = chunkBounds(chunk.id, DefaultChunkSize);
    ChunkInstance orphan;
    orphan.group = 4;
    chunk.instances.push_back(orphan);

    Chunk decoded;
    CHECK(decodeChunk(encodeChunk(chunk), decoded).has_value());

    Chunk overrun;
    overrun.id = ChunkId{0, 0, 0};
    overrun.bounds = chunkBounds(overrun.id, DefaultChunkSize);
    ChunkInstance tagged;
    tagged.firstTag = 0;
    tagged.tagCount = 3;
    overrun.instances.push_back(tagged);
    CHECK(decodeChunk(encodeChunk(overrun), decoded).has_value());
}

TEST_CASE("the index carries a cell's real extent, not only its square")
{
    seedRealCatalog();

    // An atomic model lives in one cell however far it spreads (ADR 0053), so a
    // cell may be wider than its footprint -- and an index that described only
    // the footprint would keep the overhang outside the loading ring until the
    // cell centre came in.
    ChunkIndex index;
    index.chunkSize = 256.0f;

    ChunkIndexEntry wide;
    wide.id = ChunkId{1, 0, 1};
    wide.bounds = chunkBounds(wide.id, index.chunkSize);
    wide.bounds.min.x -= 40.0;
    wide.bounds.max.z += 12.0;
    wide.bounds.min.y = -3.0;
    wide.bounds.max.y = 21.0;
    wide.urn = "asset://world/gate.lchunk";
    wide.instanceCount = 3;
    index.chunks.push_back(wide);

    ChunkIndex back;
    REQUIRE(!readChunkIndex(writeChunkIndex(index), back).has_value());
    REQUIRE(back.chunks.size() == 1);
    CHECK(back.chunks[0].bounds.min.x == doctest::Approx(wide.bounds.min.x));
    CHECK(back.chunks[0].bounds.max.z == doctest::Approx(wide.bounds.max.z));
    CHECK(back.chunks[0].bounds.min.y == doctest::Approx(-3.0));
}

TEST_CASE("an index written before cells had a real extent still reads")
{
    seedRealCatalog();

    // The footprint is the FALLBACK, so a row with no horizontal bounds means
    // what it always meant rather than an empty box.
    const std::string older = R"({"format":"luaug-chunk-index","version":1,"chunkSize":256,"chunks":[)"
                              R"({"x":2,"z":0,"layer":0,"urn":"asset://world/c.lchunk","instances":1,"bytes":8,)"
                              R"("minY":-1,"maxY":1}]})";
    ChunkIndex index;
    REQUIRE(!readChunkIndex(older, index).has_value());
    REQUIRE(index.chunks.size() == 1);
    CHECK(index.chunks[0].bounds.min.x == doctest::Approx(512.0));
    CHECK(index.chunks[0].bounds.max.x == doctest::Approx(768.0));
}
