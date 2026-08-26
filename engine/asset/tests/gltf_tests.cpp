#include "luaug/asset/gltf.h"
#include "luaug/core/i18n.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using luaug::asset::AlphaMode;
using luaug::asset::GltfImportOptions;
using luaug::asset::importGltf;
using luaug::asset::MaterialDef;
using luaug::asset::Mesh;
using luaug::asset::Model;
using luaug::asset::Submesh;
using luaug::core::AABB;
using luaug::core::f32;
using luaug::core::u32;
using luaug::core::Vec3;

namespace {

// The catalog has to be loaded or every message below is a bare key, and a test
// asserting on message text would then assert on nothing (M2's Finding 11).
struct CatalogFixture
{
    CatalogFixture()
    {
        const auto result = luaug::core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
        REQUIRE(result.ok);
    }
};

std::filesystem::path dataDirectory()
{
    return std::filesystem::path(LUAUG_ASSET_TEST_DATA);
}

std::vector<std::byte> readFixture(const char* name)
{
    const auto path = dataDirectory() / name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), path.string());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
        bytes[i] = static_cast<std::byte>(raw[i]);
    return bytes;
}

std::vector<std::byte> toBytes(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
        bytes[i] = static_cast<std::byte>(text[i]);
    return bytes;
}

std::string fixtureText(const char* name)
{
    const std::vector<std::byte> bytes = readFixture(name);
    std::string text(bytes.size(), '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i)
        text[i] = static_cast<char>(bytes[i]);
    return text;
}

// Returns how many replacements happened, so a test that edits a fixture can
// REQUIRE that its edit landed rather than silently asserting against the
// unmodified file.
std::size_t replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    std::size_t count = 0;
    std::size_t at = text.find(from);
    while (at != std::string::npos) {
        text.replace(at, from.size(), to);
        at = text.find(from, at + to.size());
        ++count;
    }
    return count;
}

// f32 comparison with an explicit tolerance; promoting to f64 for a doctest
// Approx would be a -Wdouble-promotion error (math_tests.cpp says the same).
constexpr f32 kEpsilon = 1e-5f;

bool near(f32 a, f32 b) noexcept
{
    return std::fabs(a - b) <= kEpsilon;
}

bool near(Vec3 a, Vec3 b) noexcept
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

bool near(const AABB& a, const AABB& b) noexcept
{
    return near(a.min, b.min) && near(a.max, b.max);
}

// A triangle as three positions, rotated so the lexicographically smallest
// vertex comes first. Winding is preserved by the rotation, so two meshes with
// the same triangles compare equal however the optimizer reordered them.
using Triangle = std::array<Vec3, 3>;

bool before(Vec3 a, Vec3 b) noexcept
{
    if (a.x != b.x)
        return a.x < b.x;
    if (a.y != b.y)
        return a.y < b.y;
    return a.z < b.z;
}

bool beforeTriangle(const Triangle& a, const Triangle& b) noexcept
{
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(a[i] == b[i]))
            return before(a[i], b[i]);
    }
    return false;
}

// The submesh's triangles in the order it draws them, each canonicalized so a
// rotated-but-identical triangle compares equal.
std::vector<Triangle> trianglesInDrawOrder(const Mesh& mesh, const Submesh& submesh)
{
    std::vector<Triangle> triangles;
    for (u32 offset = 0; offset + 2 < submesh.indexCount; offset += 3) {
        Triangle triangle{mesh.vertices[mesh.indices[submesh.firstIndex + offset]].position,
                          mesh.vertices[mesh.indices[submesh.firstIndex + offset + 1]].position,
                          mesh.vertices[mesh.indices[submesh.firstIndex + offset + 2]].position};

        std::size_t first = 0;
        for (std::size_t i = 1; i < 3; ++i) {
            if (before(triangle[i], triangle[first]))
                first = i;
        }
        triangles.push_back(Triangle{triangle[first], triangle[(first + 1) % 3], triangle[(first + 2) % 3]});
    }
    return triangles;
}

// The same triangles as a set, for comparing two meshes the optimizer is free
// to have reordered.
std::vector<Triangle> trianglesOf(const Mesh& mesh, const Submesh& submesh)
{
    std::vector<Triangle> triangles = trianglesInDrawOrder(mesh, submesh);
    std::sort(triangles.begin(), triangles.end(), beforeTriangle);
    return triangles;
}

// The vertex a submesh draws at `position`, so a test can name a corner instead
// of an index the optimizer is free to move.
const luaug::asset::Vertex& vertexAt(const Mesh& mesh, const Submesh& submesh, Vec3 position)
{
    for (u32 offset = 0; offset < submesh.indexCount; ++offset) {
        const luaug::asset::Vertex& vertex = mesh.vertices[mesh.indices[submesh.firstIndex + offset]];
        if (near(vertex.position, position))
            return vertex;
    }
    REQUIRE_MESSAGE(false, "no vertex at the requested position");
    return mesh.vertices[0];
}

void appendU32(std::vector<std::byte>& bytes, u32 value)
{
    for (std::size_t shift = 0; shift < 4; ++shift)
        bytes.push_back(static_cast<std::byte>((value >> (shift * 8)) & 0xFFu));
}

// A GLB container around a JSON chunk and an optional BIN chunk, built here
// rather than committed: a `.glb` is not diffable, and the layout is four
// lengths and two tags that are clearer as code than as a hex dump.
std::vector<std::byte> makeGlb(std::string_view json, const std::vector<std::byte>& bin)
{
    std::string paddedJson(json);
    while (paddedJson.size() % 4 != 0)
        paddedJson.push_back(' ');

    std::vector<std::byte> paddedBin = bin;
    while (paddedBin.size() % 4 != 0)
        paddedBin.push_back(std::byte{0});

    const u32 total = static_cast<u32>(12 + 8 + paddedJson.size() + (paddedBin.empty() ? 0 : 8 + paddedBin.size()));

    std::vector<std::byte> glb;
    appendU32(glb, 0x46546C67u); // "glTF"
    appendU32(glb, 2u);
    appendU32(glb, total);

    appendU32(glb, static_cast<u32>(paddedJson.size()));
    appendU32(glb, 0x4E4F534Au); // "JSON"
    for (const char c : paddedJson)
        glb.push_back(static_cast<std::byte>(c));

    if (!paddedBin.empty()) {
        appendU32(glb, static_cast<u32>(paddedBin.size()));
        appendU32(glb, 0x004E4942u); // "BIN\0"
        glb.insert(glb.end(), paddedBin.begin(), paddedBin.end());
    }
    return glb;
}

void appendFloats(std::vector<std::byte>& bytes, std::initializer_list<f32> values)
{
    for (const f32 value : values) {
        std::array<std::byte, sizeof(f32)> raw{};
        std::memcpy(raw.data(), &value, sizeof(f32));
        bytes.insert(bytes.end(), raw.begin(), raw.end());
    }
}

void appendU16(std::vector<std::byte>& bytes, std::initializer_list<std::uint16_t> values)
{
    for (const std::uint16_t value : values) {
        bytes.push_back(static_cast<std::byte>(value & 0xFFu));
        bytes.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
    }
}

// A GLB holding one grid of quads split into two primitives over ONE shared
// vertex buffer: the lower triangle of every quad is the first primitive, the
// upper triangle the second.
//
// Built rather than committed, and deliberately much larger than the hand-
// written fixtures. Two things make it the right shape for testing the
// optimizer: it has enough triangles that meshoptimizer genuinely rearranges
// them, and the two primitives are one connected surface in index space, so an
// optimizer let loose on the whole index buffer at once WOULD move a triangle
// from one draw into the other. A pair of disjoint grids would not -- the cache
// optimizer walks one connected component at a time and would happen to keep
// them apart, and the test would pass while proving nothing.
constexpr std::size_t kGridSide = 17; // vertices per row, so 16 x 16 quads
constexpr std::size_t kGridVertices = kGridSide * kGridSide;
constexpr std::size_t kGridQuads = (kGridSide - 1) * (kGridSide - 1);

std::vector<std::byte> makeGridGlb()
{
    std::vector<std::byte> bin;
    for (std::size_t row = 0; row < kGridSide; ++row) {
        for (std::size_t column = 0; column < kGridSide; ++column)
            appendFloats(bin, {static_cast<f32>(column), static_cast<f32>(row), 0.0f});
    }
    for (std::size_t vertex = 0; vertex < kGridVertices; ++vertex)
        appendFloats(bin, {0.0f, 0.0f, 1.0f});

    // Counter-clockwise seen from +z, which is glTF's front face. The lower
    // triangles go in first and the upper ones after, as two index ranges over
    // the same vertices.
    for (const bool upper : {false, true}) {
        for (std::size_t row = 0; row + 1 < kGridSide; ++row) {
            for (std::size_t column = 0; column + 1 < kGridSide; ++column) {
                const auto corner = static_cast<std::uint16_t>(row * kGridSide + column);
                const auto right = static_cast<std::uint16_t>(corner + 1);
                const auto above = static_cast<std::uint16_t>(corner + kGridSide);
                const auto diagonal = static_cast<std::uint16_t>(corner + kGridSide + 1);
                if (upper)
                    appendU16(bin, {corner, diagonal, above});
                else
                    appendU16(bin, {corner, right, diagonal});
            }
        }
    }

    const std::size_t positionBytes = kGridVertices * 12;
    const std::size_t indexBytes = kGridQuads * 3 * 2;
    REQUIRE(bin.size() == positionBytes * 2 + indexBytes * 2);

    const std::string side = std::to_string(kGridSide - 1);
    const std::string vertexCount = std::to_string(kGridVertices);
    const std::string indexCount = std::to_string(kGridQuads * 3);
    const std::string positions = std::to_string(positionBytes);
    const std::string indices = std::to_string(indexBytes);
    const std::string json = std::string(R"({
      "asset": { "version": "2.0" },
      "scene": 0,
      "scenes": [ { "nodes": [ 0 ] } ],
      "nodes": [ { "mesh": 0 } ],
      "meshes": [ { "primitives": [
        { "attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 2, "material": 0 },
        { "attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 3, "material": 1 } ] } ],
      "materials": [ { "name": "Lower" }, { "name": "Upper" } ],
      "accessors": [
        { "bufferView": 0, "componentType": 5126, "count": )") +
                             vertexCount + R"(, "type": "VEC3", "min": [ 0.0, 0.0, 0.0 ], "max": [ )" + side + ", " +
                             side + R"(, 0.0 ] },
        { "bufferView": 1, "componentType": 5126, "count": )" +
                             vertexCount + R"(, "type": "VEC3" },
        { "bufferView": 2, "componentType": 5123, "count": )" +
                             indexCount + R"(, "type": "SCALAR" },
        { "bufferView": 3, "componentType": 5123, "count": )" +
                             indexCount + R"(, "type": "SCALAR" }
      ],
      "bufferViews": [
        { "buffer": 0, "byteOffset": 0, "byteLength": )" +
                             positions + R"( },
        { "buffer": 0, "byteOffset": )" +
                             positions + R"(, "byteLength": )" + positions + R"( },
        { "buffer": 0, "byteOffset": )" +
                             std::to_string(positionBytes * 2) + R"(, "byteLength": )" + indices + R"( },
        { "buffer": 0, "byteOffset": )" +
                             std::to_string(positionBytes * 2 + indexBytes) + R"(, "byteLength": )" + indices + R"( }
      ],
      "buffers": [ { "byteLength": )" +
                             std::to_string(bin.size()) + R"( } ]
    })";

    return makeGlb(json, bin);
}

GltfImportOptions unoptimized()
{
    // The value assertions pin per-vertex data, and the optimizer is free to
    // renumber vertices -- which is exactly what `optimize` exists to be turned
    // off for (gltf.h). The optimizer's own behaviour has its own test.
    GltfImportOptions options;
    options.optimize = false;
    return options;
}

} // namespace

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a quad imports with the counts, bounds and attributes the file declares")
{
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("quad.gltf"), dataDirectory(), unoptimized(), model).has_value());

    CHECK(model.mesh.vertices.size() == 4u);
    CHECK(model.mesh.indices.size() == 6u);
    REQUIRE(model.mesh.submeshes.size() == 1u);
    CHECK(model.mesh.submeshes[0].firstIndex == 0u);
    CHECK(model.mesh.submeshes[0].indexCount == 6u);

    // The file names no material, so the importer appends glTF's default one --
    // "a draw with no material is a draw the renderer cannot make" (model.h).
    REQUIRE(model.materials.size() == 1u);
    CHECK(model.materials[0].shader == "pbr");
    CHECK(model.mesh.submeshes[0].material == 0u);
    CHECK(model.images.empty());

    const AABB expected = AABB::fromMinMax(Vec3{-1.0f, -1.0f, 0.0f}, Vec3{1.0f, 1.0f, 0.0f});
    CHECK(near(model.mesh.bounds, expected));
    CHECK(near(model.mesh.submeshes[0].bounds, expected));

    for (const luaug::asset::Vertex& vertex : model.mesh.vertices)
        CHECK(near(vertex.normal, Vec3{0.0f, 0.0f, 1.0f}));

    // The UV set is the half of the layout a transposed row or a dropped
    // attribute would corrupt without changing any count.
    const luaug::asset::Vertex& corner = vertexAt(model.mesh, model.mesh.submeshes[0], Vec3{-1.0f, -1.0f, 0.0f});
    CHECK(near(corner.uv[0], 0.0f));
    CHECK(near(corner.uv[1], 1.0f));

    const std::vector<Triangle> triangles = trianglesOf(model.mesh, model.mesh.submeshes[0]);
    REQUIRE(triangles.size() == 2u);
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: two primitives with different materials become two submeshes")
{
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("two_materials.gltf"), dataDirectory(), unoptimized(), model).has_value());

    REQUIRE(model.mesh.submeshes.size() == 2u);
    CHECK(model.mesh.vertices.size() == 7u);
    CHECK(model.mesh.indices.size() == 9u);

    CHECK(model.mesh.submeshes[0].firstIndex == 0u);
    CHECK(model.mesh.submeshes[0].indexCount == 6u);
    CHECK(model.mesh.submeshes[1].firstIndex == 6u);
    CHECK(model.mesh.submeshes[1].indexCount == 3u);

    REQUIRE(model.materials.size() == 2u);
    CHECK(model.mesh.submeshes[0].material == 0u);
    CHECK(model.mesh.submeshes[1].material == 1u);
    CHECK(model.materials[0].name == "Crimson");
    CHECK(model.materials[1].name == "Verdant");

    // Every scalar the file declares, so a field read from the wrong place in
    // fastgltf's material shows up here rather than in a screenshot.
    CHECK(near(model.materials[0].baseColorFactor.r, 0.75f));
    CHECK(near(model.materials[0].baseColorFactor.g, 0.25f));
    CHECK(near(model.materials[0].baseColorFactor.b, 0.125f));
    CHECK(near(model.materials[0].baseColorAlpha, 0.5f));
    CHECK(near(model.materials[0].metallicFactor, 0.25f));
    CHECK(near(model.materials[0].roughnessFactor, 0.75f));
    CHECK(model.materials[0].alphaMode == AlphaMode::Mask);
    CHECK(near(model.materials[0].alphaCutoff, 0.125f));
    CHECK(model.materials[0].doubleSided);
    CHECK_FALSE(model.materials[0].baseColor.present());

    CHECK(model.materials[1].alphaMode == AlphaMode::Blend);
    CHECK_FALSE(model.materials[1].doubleSided);
    CHECK(near(model.materials[1].emissiveFactor.r, 0.25f));
    CHECK(near(model.materials[1].emissiveFactor.g, 0.5f));
    CHECK(near(model.materials[1].emissiveFactor.b, 0.75f));

    // Each submesh bounds its own geometry, not the model's: the quad is at
    // z = 0 and the triangle at z = 1, so one box covering both would be the
    // bug this catches.
    CHECK(near(model.mesh.submeshes[0].bounds, AABB::fromMinMax(Vec3{-1.0f, -1.0f, 0.0f}, Vec3{1.0f, 1.0f, 0.0f})));
    CHECK(near(model.mesh.submeshes[1].bounds, AABB::fromMinMax(Vec3{0.0f, 0.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f})));
    CHECK(near(model.mesh.bounds, AABB::fromMinMax(Vec3{-1.0f, -1.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f})));
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a node transform is baked into positions, normals and tangents")
{
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("scaled_node.gltf"), dataDirectory(), unoptimized(), model).has_value());

    REQUIRE(model.mesh.vertices.size() == 3u);
    REQUIRE(model.mesh.submeshes.size() == 1u);

    // The node chain is translate(10,0,0) over scale(2,1,1), so a position
    // (1,2,3) lands at (12,2,3). A test that only checked the translation would
    // pass with the scale dropped, and vice versa.
    const luaug::asset::Vertex& moved = vertexAt(model.mesh, model.mesh.submeshes[0], Vec3{12.0f, 2.0f, 3.0f});
    CHECK(near(moved.position, Vec3{12.0f, 2.0f, 3.0f}));
    CHECK(near(model.mesh.bounds, AABB::fromMinMax(Vec3{10.0f, 0.0f, 0.0f}, Vec3{12.0f, 2.0f, 3.0f})));

    // The file's normal is (1,1,0)/sqrt(2). Baked through the inverse-transpose
    // of scale(2,1,1) -- diag(0.5,1,1) -- it normalizes to (1,2,0)/sqrt(5).
    // Baking it through the transform instead would give (2,1,0)/sqrt(5), which
    // is the same numbers with x and y swapped: the failure mode that looks
    // right on a uniformly scaled model and wrong on this one.
    const f32 fifth = 1.0f / std::sqrt(5.0f);
    for (const luaug::asset::Vertex& vertex : model.mesh.vertices) {
        CHECK(near(vertex.normal, Vec3{fifth, 2.0f * fifth, 0.0f}));
        CHECK_FALSE(near(vertex.normal, Vec3{2.0f * fifth, fifth, 0.0f}));

        // The tangent lies in the surface and follows the transform itself, so
        // (1,-1,0)/sqrt(2) becomes (2,-1,0)/sqrt(5) -- the mirror image of what
        // the normal did, which is what makes the pair a real check.
        CHECK(near(vertex.tangent[0], 2.0f * fifth));
        CHECK(near(vertex.tangent[1], -fifth));
        CHECK(near(vertex.tangent[2], 0.0f));
        // Handedness is carried through, never recomputed from a transform.
        CHECK(near(vertex.tangent[3], 1.0f));

        // A translation applied to a direction is the other classic mistake,
        // and it would push every normal past x = 10.
        CHECK(std::fabs(vertex.normal.x) <= 1.0f + kEpsilon);
    }
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a mirroring node reverses the winding of what it holds")
{
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("mirrored_node.gltf"), dataDirectory(), unoptimized(), model).has_value());

    REQUIRE(model.mesh.vertices.size() == 3u);
    REQUIRE(model.mesh.indices.size() == 3u);

    // scale(-1, 1, 1) turns the triangle over. The file's normal is +z and the
    // inverse-transpose leaves it there, so if the winding were left alone the
    // vertices would wind clockwise around a normal that says counter-clockwise
    // -- back-face culling would then drop the triangle, or light it inside
    // out. glTF 2.0 §3.7.4 says to reverse the winding, and this is that.
    const Vec3 a = model.mesh.vertices[model.mesh.indices[0]].position;
    const Vec3 b = model.mesh.vertices[model.mesh.indices[1]].position;
    const Vec3 c = model.mesh.vertices[model.mesh.indices[2]].position;
    const Vec3 wound = luaug::core::normalize(luaug::core::cross(b - a, c - a));

    CHECK(near(model.mesh.vertices[0].normal, Vec3{0.0f, 0.0f, 1.0f}));
    CHECK(near(wound, model.mesh.vertices[0].normal));
    CHECK(near(model.mesh.bounds, AABB::fromMinMax(Vec3{-1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f})));
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: an accessor that reads past its own buffer view is an error")
{
    // Both 48-byte views -- positions and normals -- are halved while the
    // accessors still claim four elements of twelve bytes each. Nothing in
    // fastgltf's own validation looks at this, so without the importer's bounds
    // check the read runs off the end of the buffer.
    std::string text = fixtureText("quad.gltf");
    REQUIRE(replaceAll(text, "\"byteLength\": 48", "\"byteLength\": 24") == 2u);

    Model model;
    const auto error = importGltf(toBytes(text), dataDirectory(), unoptimized(), model);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.gltf.err.accessor_out_of_range") != std::string::npos);
    CHECK(model.mesh.vertices.empty());

    // The other shape of the same lie: an element count far larger than the
    // view could ever hold, which is also what would overflow the stride
    // arithmetic if it were done before the count was sanity-checked.
    std::string huge = fixtureText("quad.gltf");
    REQUIRE(replaceAll(huge, "\"count\": 6", "\"count\": 6000000000") == 1u);
    Model overflowing;
    CHECK(importGltf(toBytes(huge), dataDirectory(), unoptimized(), overflowing).has_value());
    CHECK(overflowing.mesh.vertices.empty());
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a primitive with no normals gets flat ones, and is de-indexed to carry them")
{
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("flat_normals.gltf"), dataDirectory(), unoptimized(), model).has_value());

    // Four positions and six indices in the file. A flat normal belongs to a
    // face, so the two triangles cannot share the diagonal's vertices: six
    // vertices out, one per index.
    CHECK(model.mesh.vertices.size() == 6u);
    REQUIRE(model.mesh.indices.size() == 6u);
    for (u32 index = 0; index < 6u; ++index)
        CHECK(model.mesh.indices[index] == index);

    for (const luaug::asset::Vertex& vertex : model.mesh.vertices)
        CHECK(near(vertex.normal, Vec3{0.0f, 0.0f, 1.0f}));

    // The quad still covers the same ground; de-indexing must not move a vertex.
    CHECK(near(model.mesh.bounds, AABB::fromMinMax(Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 0.0f})));

    // With generation switched off the primitive keeps its four shared vertices
    // and its normals stay zero -- which is what proves the six above came from
    // the generator rather than from the file.
    GltfImportOptions bare = unoptimized();
    bare.generateMissingNormals = false;
    Model plain;
    REQUIRE_FALSE(importGltf(readFixture("flat_normals.gltf"), dataDirectory(), bare, plain).has_value());
    CHECK(plain.mesh.vertices.size() == 4u);
    CHECK(near(plain.mesh.vertices[0].normal, Vec3{0.0f, 0.0f, 0.0f}));
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: an image two materials share is decoded once")
{
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("textured.gltf"), dataDirectory(), unoptimized(), model).has_value());

    REQUIRE(model.materials.size() == 2u);
    // Three texture objects across the two materials, but only two images: the
    // first material's base colour and the second's normal map are two textures
    // over one image, and it is decoded once.
    REQUIRE(model.images.size() == 2u);
    for (const luaug::asset::Image& image : model.images) {
        CHECK(image.valid());
        CHECK(image.width == 2u);
        CHECK(image.height == 2u);
    }

    REQUIRE(model.materials[0].baseColor.present());
    REQUIRE(model.materials[1].normal.present());
    REQUIRE(model.materials[1].emissive.present());
    CHECK(model.materials[0].baseColor.image == model.materials[1].normal.image);
    CHECK(model.materials[1].emissive.image != model.materials[0].baseColor.image);
    CHECK(model.materials[0].baseColor.uvSet == 0u);
    CHECK(near(model.materials[1].normalScale, 0.5f));

    // The first image is a sibling file, resolved against the directory the
    // caller passed; the second is a data URI inside the document. Both decode,
    // and the sibling one is what would break if `baseDirectory` were ignored.
    CHECK(model.images[0].pixels == model.images[1].pixels);
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: tangents are generated only where a normal map needs them")
{
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("textured.gltf"), dataDirectory(), unoptimized(), model).has_value());
    REQUIRE(model.mesh.submeshes.size() == 2u);

    // Same geometry and same UVs in both primitives. The first material samples
    // only a base colour, so nothing needs a tangent and the vertex keeps the
    // identity one; the second has a normal map, so tangents are generated.
    const luaug::asset::Vertex& plain = vertexAt(model.mesh, model.mesh.submeshes[0], Vec3{0.0f, 0.0f, 0.0f});
    CHECK(near(plain.tangent[0], 0.0f));
    CHECK(near(plain.tangent[1], 0.0f));
    CHECK(near(plain.tangent[2], 0.0f));
    CHECK(near(plain.tangent[3], 1.0f));

    // U runs with +x and V runs against +y (glTF's UV origin is the top left),
    // so the tangent is +x and the bitangent points at -y -- a left-handed pair
    // against the +z normal, which is what w = -1 records.
    const luaug::asset::Vertex& bumpy = vertexAt(model.mesh, model.mesh.submeshes[1], Vec3{0.0f, 0.0f, 1.0f});
    CHECK(near(bumpy.tangent[0], 1.0f));
    CHECK(near(bumpy.tangent[1], 0.0f));
    CHECK(near(bumpy.tangent[2], 0.0f));
    CHECK(near(bumpy.tangent[3], -1.0f));

    // Switched off, the same file leaves the second primitive's tangents alone.
    GltfImportOptions bare = unoptimized();
    bare.generateMissingTangents = false;
    Model plainModel;
    REQUIRE_FALSE(importGltf(readFixture("textured.gltf"), dataDirectory(), bare, plainModel).has_value());
    const luaug::asset::Vertex& untouched =
        vertexAt(plainModel.mesh, plainModel.mesh.submeshes[1], Vec3{0.0f, 0.0f, 1.0f});
    CHECK(near(untouched.tangent[0], 0.0f));
    CHECK(near(untouched.tangent[3], 1.0f));
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: the optimizer reorders without changing the mesh it was given")
{
    Model plain;
    Model optimized;
    REQUIRE_FALSE(importGltf(readFixture("two_materials.gltf"), dataDirectory(), unoptimized(), plain).has_value());
    REQUIRE_FALSE(
        importGltf(readFixture("two_materials.gltf"), dataDirectory(), GltfImportOptions{}, optimized).has_value());

    REQUIRE(optimized.mesh.submeshes.size() == plain.mesh.submeshes.size());
    CHECK(optimized.mesh.indices.size() == plain.mesh.indices.size());
    // Vertex-fetch drops vertices nothing references; nothing here is unused,
    // so the count must survive.
    CHECK(optimized.mesh.vertices.size() == plain.mesh.vertices.size());
    CHECK(near(optimized.mesh.bounds, plain.mesh.bounds));

    for (std::size_t index = 0; index < plain.mesh.submeshes.size(); ++index) {
        // The index ranges are what the renderer draws with, and the cache and
        // overdraw passes run inside a range precisely so they stay put.
        CHECK(optimized.mesh.submeshes[index].firstIndex == plain.mesh.submeshes[index].firstIndex);
        CHECK(optimized.mesh.submeshes[index].indexCount == plain.mesh.submeshes[index].indexCount);
        CHECK(optimized.mesh.submeshes[index].material == plain.mesh.submeshes[index].material);
        CHECK(near(optimized.mesh.submeshes[index].bounds, plain.mesh.submeshes[index].bounds));

        const std::vector<Triangle> before = trianglesOf(plain.mesh, plain.mesh.submeshes[index]);
        const std::vector<Triangle> after = trianglesOf(optimized.mesh, optimized.mesh.submeshes[index]);
        REQUIRE(after.size() == before.size());
        for (std::size_t triangle = 0; triangle < before.size(); ++triangle) {
            for (std::size_t corner = 0; corner < 3; ++corner)
                CHECK(near(after[triangle][corner], before[triangle][corner]));
        }
    }
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: the optimizer keeps every triangle inside the submesh that owns it")
{
    // The three-triangle fixture above is too small for meshoptimizer to move
    // anything, so the guard that matters -- running the cache and overdraw
    // passes per index range rather than over the whole buffer -- is not
    // exercised by it. This grid is.
    const std::vector<std::byte> glb = makeGridGlb();

    Model plain;
    Model optimized;
    REQUIRE_FALSE(importGltf(glb, dataDirectory(), unoptimized(), plain).has_value());
    REQUIRE_FALSE(importGltf(glb, dataDirectory(), GltfImportOptions{}, optimized).has_value());

    REQUIRE(optimized.mesh.submeshes.size() == 2u);
    CHECK(optimized.mesh.indices.size() == plain.mesh.indices.size());
    // Each primitive gets its own copy of the shared grid, and in each copy one
    // corner belongs only to the other primitive's triangles -- the top-left
    // vertex is never a lower triangle's, the bottom-right never an upper one's.
    // The vertex-fetch pass drops exactly those two, which is also what proves
    // that pass ran at all.
    CHECK(optimized.mesh.vertices.size() + 2u == plain.mesh.vertices.size());
    // If this ever stops holding, the mesh got too small or too regular for the
    // optimizer to touch, and every assertion below has quietly stopped testing
    // the optimized path.
    REQUIRE(optimized.mesh.indices != plain.mesh.indices);

    for (std::size_t index = 0; index < 2u; ++index) {
        const Submesh& submesh = optimized.mesh.submeshes[index];
        CHECK(submesh.firstIndex == plain.mesh.submeshes[index].firstIndex);
        CHECK(submesh.indexCount == plain.mesh.submeshes[index].indexCount);
        CHECK(near(submesh.bounds, plain.mesh.submeshes[index].bounds));

        // The cache pass changes which triangle is drawn when, which the
        // vertex-fetch pass alone would not -- without this the assertions
        // below would still hold with the reordering passes deleted.
        REQUIRE(trianglesInDrawOrder(optimized.mesh, submesh) !=
                trianglesInDrawOrder(plain.mesh, plain.mesh.submeshes[index]));

        const std::vector<Triangle> before = trianglesOf(plain.mesh, plain.mesh.submeshes[index]);
        const std::vector<Triangle> after = trianglesOf(optimized.mesh, submesh);
        REQUIRE(after.size() == before.size());
        REQUIRE(after.size() == kGridQuads);
        for (std::size_t triangle = 0; triangle < before.size(); ++triangle) {
            for (std::size_t corner = 0; corner < 3; ++corner)
                CHECK(near(after[triangle][corner], before[triangle][corner]));
        }

        // A lower triangle's three rows sum to 3r + 1 and an upper one's to
        // 3r + 2, whatever quad it came from -- so this is "did a triangle from
        // the other draw land in this range", asked of every triangle rather
        // than of the set as a whole.
        const int expected = index == 0 ? 1 : 2;
        for (const Triangle& triangle : after) {
            const int rows = static_cast<int>(std::lround(triangle[0].y + triangle[1].y + triangle[2].y));
            CHECK(rows % 3 == expected);
        }
    }
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a failure partway through discards what was already imported")
{
    // The second primitive is switched to POINTS, so the first one has already
    // become a submesh, a material and a vertex buffer by the time the import
    // gives up. "Returns an error rather than a partial model" is only true if
    // all of that is thrown away.
    std::string text = fixtureText("two_materials.gltf");
    REQUIRE(replaceAll(text, "\"indices\": 5", "\"mode\": 0, \"indices\": 5") == 1u);

    Model model;
    const auto error = importGltf(toBytes(text), dataDirectory(), unoptimized(), model);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.gltf.err.unsupported_topology") != std::string::npos);

    CHECK(model.mesh.vertices.empty());
    CHECK(model.mesh.indices.empty());
    CHECK(model.mesh.submeshes.empty());
    CHECK(model.materials.empty());
    CHECK(luaug::core::isEmpty(model.mesh.bounds));
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: importing the same file twice gives byte-identical geometry")
{
    // R10: same file in, same model out. A hash container reached by the
    // importer would show up here as a run-to-run difference in which material
    // or image landed in which slot.
    Model first;
    Model second;
    REQUIRE_FALSE(importGltf(readFixture("textured.gltf"), dataDirectory(), GltfImportOptions{}, first).has_value());
    REQUIRE_FALSE(importGltf(readFixture("textured.gltf"), dataDirectory(), GltfImportOptions{}, second).has_value());

    REQUIRE(first.mesh.vertices.size() == second.mesh.vertices.size());
    CHECK(std::memcmp(first.mesh.vertices.data(), second.mesh.vertices.data(),
                      first.mesh.vertices.size() * sizeof(luaug::asset::Vertex)) == 0);
    CHECK(first.mesh.indices == second.mesh.indices);
    REQUIRE(first.materials.size() == second.materials.size());
    for (std::size_t index = 0; index < first.materials.size(); ++index) {
        CHECK(first.materials[index].name == second.materials[index].name);
        CHECK(first.materials[index].baseColor.image == second.materials[index].baseColor.image);
        CHECK(first.materials[index].normal.image == second.materials[index].normal.image);
        CHECK(first.materials[index].emissive.image == second.materials[index].emissive.image);
    }
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a GLB container reads the same document a .gltf does")
{
    // The JSON is the fixture's, unmodified, wrapped in a GLB with no BIN chunk
    // -- a legal `.glb` whose buffer is still a data URI. What this pins is the
    // container, so the model must come out identical to the `.gltf` import.
    Model fromJson;
    REQUIRE_FALSE(importGltf(readFixture("quad.gltf"), dataDirectory(), unoptimized(), fromJson).has_value());

    const std::vector<std::byte> glb = makeGlb(fixtureText("quad.gltf"), {});
    Model fromGlb;
    REQUIRE_FALSE(importGltf(glb, dataDirectory(), unoptimized(), fromGlb).has_value());

    REQUIRE(fromGlb.mesh.vertices.size() == fromJson.mesh.vertices.size());
    CHECK(std::memcmp(fromGlb.mesh.vertices.data(), fromJson.mesh.vertices.data(),
                      fromJson.mesh.vertices.size() * sizeof(luaug::asset::Vertex)) == 0);
    CHECK(fromGlb.mesh.indices == fromJson.mesh.indices);
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a GLB reads its geometry out of the binary chunk")
{
    constexpr std::string_view json = R"({
      "asset": { "version": "2.0" },
      "scene": 0,
      "scenes": [ { "nodes": [ 0 ] } ],
      "nodes": [ { "mesh": 0 } ],
      "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1 } ] } ],
      "accessors": [
        { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
          "min": [ 0.0, 0.0, 0.0 ], "max": [ 2.0, 3.0, 0.0 ] },
        { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
      ],
      "bufferViews": [
        { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
        { "buffer": 0, "byteOffset": 36, "byteLength": 6 }
      ],
      "buffers": [ { "byteLength": 44 } ]
    })";

    std::vector<std::byte> bin;
    appendFloats(bin, {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f});
    appendU16(bin, {0, 1, 2});
    bin.push_back(std::byte{0});
    bin.push_back(std::byte{0});
    REQUIRE(bin.size() == 44u);

    Model model;
    REQUIRE_FALSE(importGltf(makeGlb(json, bin), dataDirectory(), unoptimized(), model).has_value());

    REQUIRE(model.mesh.vertices.size() == 3u);
    CHECK(near(model.mesh.bounds, AABB::fromMinMax(Vec3{0.0f, 0.0f, 0.0f}, Vec3{2.0f, 3.0f, 0.0f})));
    // No NORMAL in the document, so the flat one is computed from the winding.
    for (const luaug::asset::Vertex& vertex : model.mesh.vertices)
        CHECK(near(vertex.normal, Vec3{0.0f, 0.0f, 1.0f}));
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a malformed file is an error and leaves the model empty")
{
    Model model;
    // Pre-filled, because the contract is that a failed import resets `out`
    // rather than leaving whatever the caller had (gltf.h).
    model.mesh.vertices.resize(3);
    model.mesh.indices.resize(3);
    model.mesh.submeshes.resize(1);
    model.materials.resize(2);
    model.images.resize(1);

    std::string truncated = fixtureText("quad.gltf");
    truncated.resize(truncated.size() / 2);

    const auto error = importGltf(toBytes(truncated), dataDirectory(), unoptimized(), model);
    REQUIRE(error.has_value());
    // Both halves: the key, and the English the catalog resolved it to. The key
    // alone would pass against a catalog that never loaded (M2's Finding 11).
    CHECK(error->message.find("asset.gltf.err.parse_failed") != std::string::npos);
    CHECK(error->message.find("Could not read the glTF file.") != std::string::npos);

    CHECK(model.mesh.vertices.empty());
    CHECK(model.mesh.indices.empty());
    CHECK(model.mesh.submeshes.empty());
    CHECK(model.materials.empty());
    CHECK(model.images.empty());
    CHECK(luaug::core::isEmpty(model.mesh.bounds));

    Model garbage;
    const std::vector<std::byte> noise(256, std::byte{0x7F});
    CHECK(importGltf(noise, dataDirectory(), unoptimized(), garbage).has_value());
    CHECK(garbage.mesh.vertices.empty());

    Model nothing;
    CHECK(importGltf({}, dataDirectory(), unoptimized(), nothing).has_value());
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a document that draws something other than triangles is refused")
{
    std::string text = fixtureText("quad.gltf");
    // Mode 0 is POINTS. The fixture's primitive is the only place this appears.
    REQUIRE(replaceAll(text, "\"indices\": 3", "\"mode\": 0, \"indices\": 3") == 1u);

    Model model;
    const auto error = importGltf(toBytes(text), dataDirectory(), unoptimized(), model);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.gltf.err.unsupported_topology") != std::string::npos);
    CHECK(model.mesh.vertices.empty());
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: an index naming a vertex the file does not have is an error")
{
    std::string text = fixtureText("quad.gltf");
    // Every vertex attribute shrinks from four elements to three, so the index
    // buffer's `3` now names a vertex that is not there. Shrinking all three
    // together keeps the attribute counts consistent, so this reaches the index
    // check rather than the mismatch one.
    REQUIRE(replaceAll(text, "\"count\": 4", "\"count\": 3") == 3u);

    Model model;
    const auto error = importGltf(toBytes(text), dataDirectory(), unoptimized(), model);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.gltf.err.index_out_of_range") != std::string::npos);
    CHECK(model.mesh.vertices.empty());
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a scene with no geometry says so instead of returning an empty mesh")
{
    constexpr std::string_view json = R"({
      "asset": { "version": "2.0" },
      "scene": 0,
      "scenes": [ { "nodes": [ 0 ] } ],
      "nodes": [ { "name": "JustATransform", "translation": [ 1.0, 2.0, 3.0 ] } ]
    })";

    Model model;
    const auto error = importGltf(toBytes(json), dataDirectory(), unoptimized(), model);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.gltf.err.no_mesh") != std::string::npos);
    CHECK(error->message.find("contains no triangle mesh") != std::string::npos);
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a node graph that is not a tree is refused instead of recursed")
{
    // Two nodes each claiming the other as a child. Traversed naively this
    // recurses until the stack runs out.
    constexpr std::string_view json = R"({
      "asset": { "version": "2.0" },
      "scene": 0,
      "scenes": [ { "nodes": [ 0 ] } ],
      "nodes": [
        { "name": "A", "children": [ 1 ] },
        { "name": "B", "children": [ 0 ] }
      ]
    })";

    Model model;
    const auto error = importGltf(toBytes(json), dataDirectory(), unoptimized(), model);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.gltf.err.node_cycle") != std::string::npos);
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a texture the file names but does not ship is an error")
{
    std::string text = fixtureText("textured.gltf");
    REQUIRE(replaceAll(text, "\"uri\": \"checker.png\"", "\"uri\": \"absent.png\"") == 1u);

    Model model;
    const auto error = importGltf(toBytes(text), dataDirectory(), unoptimized(), model);
    REQUIRE(error.has_value());
    CHECK(model.images.empty());
    CHECK(model.mesh.vertices.empty());
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: the skeleton pass does not read the images")
{
    // **`syncSkeletons` runs for every `MeshPart` in the world, on every tick,
    // and stops before a single texel is looked at** -- including for the meshes
    // it is about to conclude have no skeleton at all. Asking the parser for the
    // images anyway meant every PNG beside every model was pulled off disk, in
    // the tick, to be thrown away.
    //
    // Asserted through a file whose image is not there, because that is the one
    // way the difference is observable from outside: a pass that reads it fails,
    // and a pass that does not read it does not care.
    std::string text = fixtureText("textured.gltf");
    REQUIRE(replaceAll(text, "\"uri\": \"checker.png\"", "\"uri\": \"absent.png\"") == 1u);

    GltfImportOptions options;
    options.skeletonOnly = true;
    Model model;
    CHECK_FALSE(importGltf(toBytes(text), dataDirectory(), options, model).has_value());

    // And it is still the skeleton pass: no geometry, no images, whatever rig
    // the file had. `textured.gltf` has none, which is the ordinary case and the
    // one the cost was being paid for.
    CHECK(model.images.empty());
    CHECK(model.mesh.vertices.empty());
}

// --- Skinning and animation (M6) ---------------------------------------------

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a skinned mesh loads its skeleton, its weights and its clip")
{
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("skinned_bar.gltf"), dataDirectory(), GltfImportOptions{}, model).has_value());

    REQUIRE(model.skinned());
    REQUIRE(model.joints.size() == 2);
    REQUIRE(model.skin.size() == model.mesh.vertices.size());

    // **Parents first.** The fixture lists its joints child-first on purpose, so
    // a loader that trusted glTF's order would put the tip at index 0 and its
    // root at index 1 -- and every pose would resolve a child against a parent
    // that had not been computed yet. `Joint::parent` is documented as always
    // less than the joint's own index, and this is the case that holds it.
    CHECK(model.joints[0].name == "Root");
    CHECK(model.joints[0].parent == luaug::asset::Joint::NoParent);
    CHECK(model.joints[1].name == "Tip");
    CHECK(model.joints[1].parent == 0);

    // And the vertex stream was rewritten into that order with them. The
    // fixture weights its bottom ring to the ROOT, which is glTF slot 1 and our
    // index 0 -- so a stream that had not been remapped would say 1 here.
    bool sawRoot = false;
    for (std::size_t vertex = 0; vertex < model.mesh.vertices.size(); ++vertex) {
        if (model.mesh.vertices[vertex].position.y < 0.5f) {
            CHECK(static_cast<double>(model.skin[vertex].joints[0]) == doctest::Approx(0.0));
            CHECK(static_cast<double>(model.skin[vertex].weights[0]) == doctest::Approx(1.0));
            sawRoot = true;
        }
    }
    CHECK(sawRoot);

    REQUIRE(model.clips.size() == 1);
    CHECK(model.clips[0].name == "Bend");
    CHECK(static_cast<double>(model.clips[0].duration) == doctest::Approx(1.0));
    REQUIRE(model.clips[0].channels.size() == 1);
    CHECK(model.clips[0].channels[0].joint == 1);
    CHECK(model.clips[0].channels[0].stride == 4);
    CHECK(model.clips[0].channels[0].times.size() == 3);
    CHECK(model.clips[0].channels[0].values.size() == 12);
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: bone weights are normalized on load")
{
    // An exporter is allowed to emit weights that do not sum to one, and a
    // vertex whose influences sum to 0.98 shrinks by 2% every frame it is
    // skinned -- which reads as a mesh that slowly deflates rather than as a
    // weight problem.
    std::string text = fixtureText("skinned_bar.gltf");
    Model model;
    REQUIRE_FALSE(importGltf(toBytes(text), dataDirectory(), GltfImportOptions{}, model).has_value());

    for (const luaug::asset::SkinVertex& vertex : model.skin) {
        const f32 sum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2] + vertex.weights[3];
        CHECK(static_cast<double>(sum) == doctest::Approx(1.0).epsilon(1e-5));
    }
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: an unskinned mesh carries no skin stream at all")
{
    // The other half of Decision 11: joints and weights cost a skinned mesh
    // twenty-four bytes a vertex and cost a static one nothing.
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("quad.gltf"), dataDirectory(), GltfImportOptions{}, model).has_value());
    CHECK_FALSE(model.skinned());
    CHECK(model.skin.empty());
    CHECK(model.joints.empty());
    CHECK(model.clips.empty());
}

// --- The one archived extension this importer knows -------------------------
//
// `KHR_materials_pbrSpecularGlossiness` was superseded by metallic-roughness and
// moved to the archive, and it is still what a great many exported models
// declare -- in `extensionsRequired`, which is the half that matters: a parser
// that does not know an extension listed there refuses the whole document. A
// downloaded horse is the case that found it: five materials, every one of them
// carrying the extension and none carrying a metallic-roughness block, so
// ignoring it would have lost every texture instead.

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a required specular-glossiness extension does not refuse the file")
{
    Model model;
    // The claim, first and on its own: this used to be `UnknownRequiredExtension`
    // and no mesh at all, and a case that only checked the material would have
    // reported the failure as a missing texture.
    REQUIRE_FALSE(importGltf(readFixture("spec_gloss.gltf"), dataDirectory(), unoptimized(), model).has_value());
    CHECK(model.mesh.vertices.size() > 0u);
    REQUIRE(model.materials.size() == 1u);
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: specular-glossiness is read as the metallic-roughness it would have been")
{
    Model model;
    REQUIRE_FALSE(importGltf(readFixture("spec_gloss.gltf"), dataDirectory(), unoptimized(), model).has_value());
    REQUIRE(model.materials.size() == 1u);
    const MaterialDef& material = model.materials[0];

    // Diffuse is the base colour, factor and texture both. The fixture's
    // material has no `pbrMetallicRoughness` block at all, so every one of these
    // would be fastgltf's default -- white, fully rough, no image -- if the
    // extension were merely tolerated rather than read.
    CHECK(near(material.baseColorFactor.r, 0.25f));
    CHECK(near(material.baseColorFactor.g, 0.5f));
    CHECK(near(material.baseColorFactor.b, 0.75f));
    CHECK(near(material.baseColorAlpha, 0.5f));
    CHECK(material.baseColor.present());

    // Glossiness is the complement of roughness, and metallic is zero: the
    // extension has no metalness at all, it expresses metal through a coloured
    // specular that a renderer built on the other model cannot use.
    CHECK(near(material.roughnessFactor, 0.25f));
    CHECK(near(material.metallicFactor, 0.0f));

    // And no ORM image is invented. The extension's second texture packs
    // specular in RGB and glossiness in A, which is neither of the channels this
    // slot means -- reading it here would tint every surface by its own
    // specular, which is worse than the plain material it would have replaced.
    CHECK_FALSE(material.metallicRoughness.present());
}

// --- A rig past what the caller can pose -------------------------------------
//
// The palette is a fixed array in a uniform block, so there is a joint count
// past which a rig cannot be posed at all -- and past it a mesh does not pose
// badly, its vertices index off the end of the palette and scatter. A downloaded
// horse with 677 joints against a budget of 64 is what found it.
//
// So past the limit the import bakes the bind pose in and hands back a static
// mesh. That is the whole trade and it costs nothing that was available: a rig
// this build cannot pose is a rig whose bind pose is everything it will show.

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a rig within the budget keeps its skeleton")
{
    Model model;
    GltfImportOptions options = unoptimized();
    options.maxSkinJoints = 4;
    REQUIRE_FALSE(importGltf(readFixture("skinned_bar.gltf"), dataDirectory(), options, model).has_value());

    CHECK(model.skinned());
    CHECK(model.joints.size() == 2u);
    CHECK_FALSE(model.bakedBindPose());
    // And it carries the pose it stands in when nothing is animating it, which
    // is one matrix per joint.
    CHECK(model.restPalette.size() == 2u);
    CHECK(model.sourceJointCount == 2u);
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: a rig past the budget is baked into its bind pose")
{
    // **The fixture whose bind pose is not the identity**, which is the only one
    // that can tell a bake from a no-op: its whole scene hangs under one node
    // carrying a unit conversion and an axis swap -- the pair every FBX-derived
    // export writes -- and its inverse bind matrices do not include it. The
    // palette therefore IS that transform, which is exactly the case a
    // downloaded model has and `skinned_bar` does not.
    Model posed;
    GltfImportOptions keep = unoptimized();
    keep.maxSkinJoints = 4;
    REQUIRE_FALSE(importGltf(readFixture("skinned_under_export.gltf"), dataDirectory(), keep, posed).has_value());

    Model baked;
    GltfImportOptions bake = unoptimized();
    // One joint of budget against two: the same file, over the line.
    bake.maxSkinJoints = 1;
    REQUIRE_FALSE(importGltf(readFixture("skinned_under_export.gltf"), dataDirectory(), bake, baked).has_value());

    // The skeleton is gone and says why it went.
    CHECK(baked.bakedBindPose());
    CHECK(baked.joints.empty());
    CHECK(baked.skin.empty());
    CHECK(baked.clips.empty());
    CHECK(baked.restPalette.empty());
    // **The count survives the skeleton**, because a character that quietly
    // stopped being animatable is a bug report and one that says "2 joints,
    // this build poses 1" is an answer.
    CHECK(baked.sourceJointCount == 2u);

    // The geometry is still the same geometry: baking moves vertices, it does
    // not drop them.
    REQUIRE(baked.mesh.vertices.size() == posed.mesh.vertices.size());
    REQUIRE(baked.mesh.indices.size() == posed.mesh.indices.size());

    // And it MOVED. The fixture's joints are translated, so a bind pose baked in
    // is a mesh somewhere its raw vertices are not -- a bake that quietly did
    // nothing would pass every assertion above.
    bool moved = false;
    for (std::size_t index = 0; index < baked.mesh.vertices.size(); ++index) {
        const Vec3 before = posed.mesh.vertices[index].position;
        const Vec3 after = baked.mesh.vertices[index].position;
        if (std::abs(before.x - after.x) > 1e-4f || std::abs(before.y - after.y) > 1e-4f ||
            std::abs(before.z - after.z) > 1e-4f) {
            moved = true;
            break;
        }
    }
    CHECK(moved);

    // Normals come out of the palette scaled -- it carries whatever unit
    // conversion the export put above the skeleton -- so they are renormalised
    // rather than divided.
    for (const auto& vertex : baked.mesh.vertices) {
        const Vec3 n = vertex.normal;
        CHECK(near(std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z), 1.0f));
    }
}

TEST_CASE_FIXTURE(CatalogFixture, "gltf: no budget means no bake, whatever the rig")
{
    // Zero is "no limit", which is what every caller that is not a renderer
    // wants -- the host reads skeletons to animate them and must never be
    // handed one that was thrown away.
    Model model;
    GltfImportOptions options = unoptimized();
    options.maxSkinJoints = 0;
    REQUIRE_FALSE(importGltf(readFixture("skinned_bar.gltf"), dataDirectory(), options, model).has_value());

    CHECK(model.skinned());
    CHECK_FALSE(model.bakedBindPose());
}

// --- What an import costs -----------------------------------------------------

// Skipped unless asked for, because it reports a wall clock -- and pointed at a
// file by a CMake cache variable rather than by the environment, because nothing
// else in this engine reads an environment variable and that looks deliberate: a
// build that behaves differently depending on the shell it was started from is a
// build nobody can reason about.
//
//   cmake -S . -B <build> -DLUAUG_BENCH_GLTF=C:/path/to/scene.gltf
//   luaug_asset_tests --test-case="*what an import costs*" -nt --no-skip
//
// The default is one of this repository's own fixtures, which is three kilobytes
// and therefore answers "roughly zero" -- every question worth asking about
// import cost is a question about somebody's real model. It exists because "is
// parsing a mesh on the frame thread a problem" is a question about a NUMBER,
// and the same question about textures turned out to be worth 90 ms a frame once
// somebody measured it instead of reading the code.
TEST_CASE("what an import costs" * doctest::skip())
{
    const std::filesystem::path path(LUAUG_BENCH_GLTF);
    REQUIRE_MESSAGE(std::filesystem::exists(path), path.string());

    const auto started = std::chrono::steady_clock::now();
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
        bytes[i] = static_cast<std::byte>(raw[i]);
    const auto afterRead = std::chrono::steady_clock::now();

    luaug::asset::Model model;
    luaug::asset::GltfImportOptions options;
    const auto error = importGltf(bytes, path.parent_path(), options, model);
    const auto afterImport = std::chrono::steady_clock::now();

    const auto ms = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(to - from).count();
    };

    if (error.has_value()) {
        MESSAGE("refused: " << error->message);
        return;
    }

    MESSAGE(path.filename().string() << " " << (bytes.size() / 1024) << " KiB" << " read=" << ms(started, afterRead)
                                     << " ms" << " import=" << ms(afterRead, afterImport) << " ms" << " vertices="
                                     << model.mesh.vertices.size() << " primitives=" << model.mesh.submeshes.size()
                                     << " joints=" << model.joints.size() << " images=" << model.images.size());
}
