#include "luaug/asset/mesh_format.h"
#include "luaug/core/i18n.h"

#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace luaug::asset;
using luaug::core::engineCatalog;
using luaug::core::f32;
using luaug::core::hashText;
using luaug::core::u32;
using luaug::core::usize;

namespace {

void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A grid of quads, dense enough that meshoptimizer has something to simplify
// and enough triangles to become more than one meshlet. Built rather than
// loaded: a fixture file would make this case about glTF parsing.
[[nodiscard]] Model gridModel(u32 side, u32 materialCount = 1)
{
    Model model;
    model.mesh.vertices.reserve(static_cast<usize>(side + 1) * (side + 1));
    for (u32 z = 0; z <= side; ++z) {
        for (u32 x = 0; x <= side; ++x) {
            Vertex vertex;
            const f32 fx = static_cast<f32>(x) / static_cast<f32>(side);
            const f32 fz = static_cast<f32>(z) / static_cast<f32>(side);
            // A gentle bulge, so simplification has curvature to lose. A flat
            // grid simplifies to two triangles and proves nothing.
            vertex.position = {fx * 10.0f, (fx - 0.5f) * (fz - 0.5f) * 4.0f, fz * 10.0f};
            vertex.normal = {0.0f, 1.0f, 0.0f};
            vertex.uv[0] = fx;
            vertex.uv[1] = fz;
            model.mesh.vertices.push_back(vertex);
        }
    }

    const u32 stride = side + 1;
    std::vector<std::vector<u32>> perMaterial(materialCount);
    for (u32 z = 0; z < side; ++z) {
        for (u32 x = 0; x < side; ++x) {
            const u32 a = z * stride + x;
            const u32 b = a + 1;
            const u32 c = a + stride;
            const u32 d = c + 1;
            std::vector<u32>& target = perMaterial[(x + z) % materialCount];
            for (const u32 index : {a, c, b, b, c, d}) {
                target.push_back(index);
            }
        }
    }

    for (u32 material = 0; material < materialCount; ++material) {
        Submesh submesh;
        submesh.firstIndex = static_cast<u32>(model.mesh.indices.size());
        submesh.indexCount = static_cast<u32>(perMaterial[material].size());
        submesh.material = material;
        submesh.bounds = luaug::core::AABB::fromMinMax({0.0f, -1.0f, 0.0f}, {10.0f, 1.0f, 10.0f});
        model.mesh.indices.insert(model.mesh.indices.end(), perMaterial[material].begin(), perMaterial[material].end());
        model.mesh.submeshes.push_back(submesh);

        MaterialDef definition;
        definition.name = "material" + std::to_string(material);
        definition.baseColorFactor = {0.5f, 0.25f, 0.125f};
        definition.roughnessFactor = 0.75f;
        definition.baseColor.image = material;
        model.materials.push_back(definition);
        model.images.push_back(Image{});
    }

    model.mesh.bounds = luaug::core::AABB::fromMinMax({0.0f, -1.0f, 0.0f}, {10.0f, 1.0f, 10.0f});
    return model;
}

// meshoptimizer's index codec restores every triangle and its winding, but is
// free to ROTATE which of the three vertices a triangle starts on -- `(16, 15,
// 32)` comes back as `(15, 32, 16)`. That is the same triangle wound the same
// way, and it is invisible to a renderer; it is only visible to a test that
// compares index arrays element by element, which the first version of the
// round-trip case did.
[[nodiscard]] bool sameTriangles(const std::vector<u32>& a, const std::vector<u32>& b)
{
    if (a.size() != b.size() || (a.size() % 3) != 0) {
        return false;
    }
    for (usize i = 0; i < a.size(); i += 3) {
        const bool rotated = (a[i] == b[i] && a[i + 1] == b[i + 1] && a[i + 2] == b[i + 2]) ||
                             (a[i] == b[i + 1] && a[i + 1] == b[i + 2] && a[i + 2] == b[i]) ||
                             (a[i] == b[i + 2] && a[i + 1] == b[i] && a[i + 2] == b[i + 1]);
        if (!rotated) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<TextureSlot> slotsFor(const Model& model)
{
    std::vector<TextureSlot> slots;
    for (usize i = 0; i < model.images.size(); ++i) {
        slots.push_back(TextureSlot{hashText("texture" + std::to_string(i)), i == 0});
    }
    return slots;
}

} // namespace

TEST_CASE("a compiled mesh round-trips through the format")
{
    seedRealCatalog();

    const Model model = gridModel(16, 2);
    CompiledMesh compiled;
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, compiled).has_value());

    const std::vector<std::byte> bytes = encodeMesh(compiled);
    CompiledMesh decoded;
    REQUIRE_FALSE(decodeMesh(bytes, decoded).has_value());

    REQUIRE(decoded.vertices.size() == compiled.vertices.size());
    for (usize i = 0; i < compiled.vertices.size(); ++i) {
        // Bit-exact, not approximate: meshopt's vertex codec is lossless, and
        // an encoder that quantized here would move every world's geometry by
        // an amount nobody chose.
        CHECK(std::memcmp(&decoded.vertices[i], &compiled.vertices[i], sizeof(Vertex)) == 0);
    }

    REQUIRE(decoded.lods.size() == compiled.lods.size());
    for (usize level = 0; level < compiled.lods.size(); ++level) {
        CHECK(sameTriangles(decoded.lods[level].indices, compiled.lods[level].indices));
        REQUIRE(decoded.lods[level].submeshes.size() == compiled.lods[level].submeshes.size());
        for (usize i = 0; i < compiled.lods[level].submeshes.size(); ++i) {
            const Submesh& a = compiled.lods[level].submeshes[i];
            const Submesh& b = decoded.lods[level].submeshes[i];
            CHECK(a.firstIndex == b.firstIndex);
            CHECK(a.indexCount == b.indexCount);
            CHECK(a.material == b.material);
            CHECK(a.bounds == b.bounds);
        }
        CHECK(decoded.lods[level].error == compiled.lods[level].error);
    }

    REQUIRE(decoded.materials.size() == 2);
    CHECK(decoded.materials[0].name == "material0");
    CHECK(decoded.materials[1].name == "material1");
    CHECK(decoded.materials[0].shader == "pbr");
    CHECK(decoded.materials[0].baseColorFactor == compiled.materials[0].baseColorFactor);
    CHECK(decoded.materials[0].roughnessFactor == 0.75f);
    CHECK(decoded.materials[1].baseColor.image == 1);

    REQUIRE(decoded.images.size() == 2);
    CHECK(decoded.images[0].hash == hashText("texture0"));
    CHECK(decoded.images[0].srgb);
    CHECK_FALSE(decoded.images[1].srgb);

    CHECK(decoded.bounds == compiled.bounds);
    CHECK(decoded.meshlets.meshlets.size() == compiled.meshlets.meshlets.size());
    CHECK(decoded.meshlets.vertices == compiled.meshlets.vertices);
    CHECK(decoded.meshlets.triangles == compiled.meshlets.triangles);
}

TEST_CASE("the LOD chain shrinks and stops when it stops paying")
{
    const Model model = gridModel(24);
    CompiledMesh compiled;
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, compiled).has_value());

    REQUIRE(compiled.lods.size() >= 2);
    CHECK(compiled.lods.size() <= 4);
    CHECK(compiled.lods[0].indices.size() == model.mesh.indices.size());
    CHECK(compiled.lods[0].error == 0.0f);

    for (usize level = 1; level < compiled.lods.size(); ++level) {
        CHECK(compiled.lods[level].indices.size() < compiled.lods[level - 1].indices.size());
        // Error is ABSOLUTE, in the mesh's own units, and it must grow as
        // detail is lost. It became absolute at M7 when the runtime selector
        // arrived: it was stored divided by `meshopt_simplifyScale` where it
        // should have been multiplied, which is neither relative nor absolute
        // -- a number with no consumer is a number with no test.
        CHECK(compiled.lods[level].error >= compiled.lods[level - 1].error);
    }

    // A LEVEL ABOVE ZERO HAS A NON-ZERO ERROR, which is the property the whole
    // selector rests on: an error that stayed zero would make every level look
    // free and the coarsest one always win.
    CHECK(compiled.lods[1].error > 0.0f);

    // And it is a plausible LENGTH rather than a fraction. The grid spans one
    // unit, so simplifying it can move the surface by a fraction of a unit and
    // not by hundreds -- which is what the old division produced.
    CHECK(compiled.lods.back().error < 1.0f);
}

TEST_CASE("simplification never welds across a material boundary")
{
    const Model model = gridModel(24, 3);
    CompiledMesh compiled;
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, compiled).has_value());
    REQUIRE(compiled.lods.size() >= 2);

    for (const MeshLod& lod : compiled.lods) {
        // Every level keeps every submesh, and every submesh keeps its own
        // material: a tree whose leaves start being drawn with the trunk's
        // shader is what the per-submesh simplify exists to prevent.
        REQUIRE(lod.submeshes.size() == 3);
        for (u32 i = 0; i < 3; ++i) {
            CHECK(lod.submeshes[i].material == i);
        }
        // Contiguous and complete: the ranges tile the index buffer with no
        // gap and no overlap.
        u32 cursor = 0;
        for (const Submesh& submesh : lod.submeshes) {
            CHECK(submesh.firstIndex == cursor);
            cursor += submesh.indexCount;
        }
        CHECK(cursor == lod.indices.size());
    }
}

TEST_CASE("meshlets are emitted and describe the mesh they came from")
{
    const Model model = gridModel(20);
    CompiledMesh compiled;
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, compiled).has_value());

    // Emitted from day one and consumed by nothing (ADR 0010): an asset re-baked
    // at the milestone that finally wants them is an asset every shipped game
    // has to re-download.
    REQUIRE_FALSE(compiled.meshlets.meshlets.empty());

    usize triangles = 0;
    for (const Meshlet& meshlet : compiled.meshlets.meshlets) {
        CHECK(meshlet.vertexCount <= 64);
        CHECK(meshlet.triangleCount <= 124);
        CHECK(meshlet.vertexOffset + meshlet.vertexCount <= compiled.meshlets.vertices.size());
        CHECK(meshlet.radius > 0.0f);
        triangles += meshlet.triangleCount;
    }
    // Every triangle of LOD 0 is in exactly one meshlet.
    CHECK(triangles == compiled.lods[0].indices.size() / 3);

    // Meshlet vertex references index the real vertex buffer.
    for (const u32 index : compiled.meshlets.vertices) {
        CHECK(index < compiled.vertices.size());
    }
}

TEST_CASE("compiling and encoding the same model twice produces the same bytes")
{
    const Model model = gridModel(18, 2);

    CompiledMesh first;
    CompiledMesh second;
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, first).has_value());
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, second).has_value());

    // The property the whole asset pipeline rests on: same input, same bytes,
    // so a content hash is a name and a CI cache is legitimate.
    CHECK(encodeMesh(first) == encodeMesh(second));
}

TEST_CASE("a skinned mesh carries its skeleton and its clips")
{
    seedRealCatalog();

    Model model = gridModel(6);
    model.skin.resize(model.mesh.vertices.size());
    for (usize i = 0; i < model.skin.size(); ++i) {
        model.skin[i].joints[0] = static_cast<f32>(i % 2);
        model.skin[i].weights[0] = 1.0f;
    }

    Joint root;
    root.name = "root";
    model.joints.push_back(root);
    Joint child;
    child.name = "child";
    child.parent = 0;
    child.localBind.position = {0.0, 1.5, 0.0};
    model.joints.push_back(child);

    AnimationClip clip;
    clip.name = "walk";
    clip.duration = 1.25f;
    AnimationChannel channel;
    channel.joint = 1;
    channel.target = AnimationChannel::Target::Rotation;
    channel.stride = 4;
    channel.times = {0.0f, 0.5f, 1.25f};
    channel.values = {0, 0, 0, 1, 0, 0.1f, 0, 0.99f, 0, 0, 0, 1};
    clip.channels.push_back(channel);
    model.clips.push_back(clip);

    CompiledMesh compiled;
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, compiled).has_value());
    CHECK(compiled.skinned());

    CompiledMesh decoded;
    REQUIRE_FALSE(decodeMesh(encodeMesh(compiled), decoded).has_value());

    REQUIRE(decoded.skin.size() == compiled.skin.size());
    CHECK(std::memcmp(decoded.skin.data(), compiled.skin.data(), decoded.skin.size() * sizeof(SkinVertex)) == 0);

    REQUIRE(decoded.joints.size() == 2);
    CHECK(decoded.joints[0].name == "root");
    CHECK(decoded.joints[1].name == "child");
    CHECK(decoded.joints[1].parent == 0);
    CHECK(decoded.joints[1].localBind.position.y == doctest::Approx(1.5));

    REQUIRE(decoded.clips.size() == 1);
    CHECK(decoded.clips[0].name == "walk");
    CHECK(decoded.clips[0].duration == 1.25f);
    REQUIRE(decoded.clips[0].channels.size() == 1);
    CHECK(decoded.clips[0].channels[0].joint == 1);
    CHECK(decoded.clips[0].channels[0].target == AnimationChannel::Target::Rotation);
    CHECK(decoded.clips[0].channels[0].times == channel.times);
    CHECK(decoded.clips[0].channels[0].values == channel.values);
}

TEST_CASE("a static mesh has no skin section at all")
{
    const Model model = gridModel(6);
    CompiledMesh compiled;
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, compiled).has_value());

    CompiledMesh decoded;
    REQUIRE_FALSE(decodeMesh(encodeMesh(compiled), decoded).has_value());
    CHECK(decoded.skin.empty());
    CHECK(decoded.joints.empty());
    CHECK(decoded.clips.empty());
    CHECK_FALSE(decoded.skinned());
}

TEST_CASE("a mesh with no geometry is refused rather than compiled")
{
    seedRealCatalog();

    Model empty;
    CompiledMesh compiled;
    const auto error = compileMesh(empty, {}, {}, compiled);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.mesh.err.empty") != std::string::npos);
}

TEST_CASE("a texture hash list that does not match the images is refused")
{
    seedRealCatalog();

    const Model model = gridModel(4, 2);
    CompiledMesh compiled;
    const std::vector<TextureSlot> tooFew = {TextureSlot{hashText("only one"), false}};
    const auto error = compileMesh(model, tooFew, {}, compiled);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.mesh.err.image_count") != std::string::npos);
}

TEST_CASE("a corrupted mesh is an error and never a crash")
{
    seedRealCatalog();

    const Model model = gridModel(8, 2);
    CompiledMesh compiled;
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, compiled).has_value());
    const std::vector<std::byte> good = encodeMesh(compiled);

    // Every prefix: a mesh blob can arrive truncated exactly as a pack can.
    for (usize length = 0; length < good.size(); length += 1 + length / 16) {
        std::vector<std::byte> truncated(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
        CompiledMesh decoded;
        const auto error = decodeMesh(truncated, decoded);
        if (length < good.size()) {
            REQUIRE_MESSAGE(error.has_value(), "a prefix of length " << length << " was accepted");
        }
    }

    // **A section count nothing could hold.** This is the flip that got out: the
    // section table has no section describing it, so its count was read from the
    // header and handed straight to `reserve` -- four billion records of
    // twenty-four bytes, which is a `std::bad_alloc` escaping a decoder whose
    // whole contract is "an error and never a crash". Asserted on its own rather
    // than left to the sweep below, because whether the sweep reaches it depends
    // on how much memory the machine has: it passed on a developer box for
    // months and failed the first time CI ran it.
    {
        std::vector<std::byte> lying = good;
        REQUIRE(lying.size() > 16);
        // Bytes 12..15 are the section count, after the magic, the version and
        // the reserved word.
        for (usize at = 12; at < 16; ++at) {
            lying[at] = std::byte{0xFF};
        }
        CompiledMesh decoded;
        const auto error = decodeMesh(lying, decoded);
        REQUIRE(error.has_value());
        CHECK(error->message.find("asset.mesh.err.malformed") != std::string::npos);
    }

    // And every single-bit flip in the header and the section table, which is
    // where a bad offset would come from.
    for (usize at = 0; at < 40 + 15 * 24 && at < good.size(); ++at) {
        for (int bit = 0; bit < 8; ++bit) {
            std::vector<std::byte> corrupted = good;
            corrupted[at] ^= static_cast<std::byte>(1u << bit);
            CompiledMesh decoded;
            // The requirement is a structured answer, not a particular one: a
            // flip in a float is legitimately still a valid mesh.
            (void)decodeMesh(corrupted, decoded);
        }
    }
}

TEST_CASE("a mesh from another format is refused by name")
{
    seedRealCatalog();

    std::vector<std::byte> notAMesh(128, std::byte{0x5a});
    CompiledMesh decoded;
    const auto error = decodeMesh(notAMesh, decoded);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.mesh.err.magic") != std::string::npos);
}

TEST_CASE("an index that names a vertex the file does not contain is refused")
{
    seedRealCatalog();

    Model model = gridModel(4);
    CompiledMesh compiled;
    REQUIRE_FALSE(compileMesh(model, slotsFor(model), {}, compiled).has_value());

    // Reach past the vertex buffer by one. Encoded, so this is what a real
    // malformed file looks like rather than a hand-built struct.
    compiled.lods[0].indices[0] = static_cast<u32>(compiled.vertices.size());
    CompiledMesh decoded;
    const auto error = decodeMesh(encodeMesh(compiled), decoded);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.mesh.err.malformed") != std::string::npos);
}
