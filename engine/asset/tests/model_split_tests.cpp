// Cutting one imported file into the pieces a person selects.
#include "luaug/asset/model_split.h"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace luaug::asset;
using luaug::core::u32;

namespace {

// A model of `count` triangles, each its own submesh, each with its own three
// vertices -- so a split has something to remap and the remap can be checked.
[[nodiscard]] Model triangles(std::size_t count)
{
    Model model;
    for (std::size_t index = 0; index < count; ++index) {
        const auto base = static_cast<u32>(model.mesh.vertices.size());
        for (int corner = 0; corner < 3; ++corner) {
            Vertex vertex;
            vertex.position =
                Vec3{static_cast<float>(index) + static_cast<float>(corner), static_cast<float>(index), 0.0f};
            model.mesh.vertices.push_back(vertex);
        }
        Submesh submesh;
        submesh.firstIndex = static_cast<u32>(model.mesh.indices.size());
        submesh.indexCount = 3;
        submesh.material = static_cast<u32>(index);
        model.mesh.indices.push_back(base);
        model.mesh.indices.push_back(base + 1);
        model.mesh.indices.push_back(base + 2);
        model.mesh.submeshes.push_back(submesh);

        MaterialDef material;
        material.name = "Material" + std::to_string(index);
        model.materials.push_back(material);
    }
    return model;
}

} // namespace

TEST_CASE("a file with three pieces becomes three models")
{
    Model model = triangles(3);
    model.submeshNames = {"Body", "Mane", "Saddle"};

    const std::vector<ModelPiece> pieces = splitByPrimitive(model);

    REQUIRE(pieces.size() == 3);
    CHECK(pieces[0].name == "Body");
    CHECK(pieces[1].name == "Mane");
    CHECK(pieces[2].name == "Saddle");

    for (const ModelPiece& piece : pieces) {
        // Each piece carries ONLY its own vertices, not the whole file's. A
        // piece that kept the model's vertex buffer would be the file's memory
        // once per piece, and the simplifier would be handed vertices no index
        // in the piece names.
        CHECK(piece.model.mesh.vertices.size() == 3);
        CHECK(piece.model.mesh.indices.size() == 3);
        REQUIRE(piece.model.mesh.submeshes.size() == 1);
        CHECK(piece.model.mesh.submeshes[0].firstIndex == 0);
        CHECK(piece.model.mesh.submeshes[0].indexCount == 3);
    }

    // And each keeps the material its submesh named, by index into the material
    // list it also kept.
    CHECK(pieces[1].model.mesh.submeshes[0].material == 1);
    CHECK(pieces[1].model.materials[1].name == "Material1");
}

TEST_CASE("the indices are rebased onto the piece's own vertices")
{
    Model model = triangles(2);
    model.submeshNames = {"First", "Second"};

    const std::vector<ModelPiece> pieces = splitByPrimitive(model);

    REQUIRE(pieces.size() == 2);
    // The second piece's vertices were at 3, 4 and 5 in the file. In its own
    // model they are 0, 1 and 2 -- rebased, not carried.
    CHECK(pieces[1].model.mesh.indices[0] == 0);
    CHECK(pieces[1].model.mesh.indices[1] == 1);
    CHECK(pieces[1].model.mesh.indices[2] == 2);
    // And they are the RIGHT three vertices, which is what a wrong rebase gets
    // wrong while still producing plausible indices.
    CHECK(pieces[1].model.mesh.vertices[0].position.y == 1.0f);
}

TEST_CASE("a vertex two triangles of one piece share is kept once")
{
    Model model;
    for (int corner = 0; corner < 4; ++corner) {
        Vertex vertex;
        vertex.position = Vec3{static_cast<float>(corner), 0.0f, 0.0f};
        model.mesh.vertices.push_back(vertex);
    }
    // Two triangles sharing an edge, in one submesh.
    model.mesh.indices = {0, 1, 2, 1, 2, 3};
    Submesh submesh;
    submesh.firstIndex = 0;
    submesh.indexCount = 6;
    model.mesh.submeshes.push_back(submesh);
    model.materials.push_back(MaterialDef{});
    model.submeshNames = {"Quad"};

    const std::vector<ModelPiece> pieces = splitByPrimitive(model);

    REQUIRE(pieces.size() == 1);
    // Four, not six: the remap is per source vertex, so a shared one is one.
    CHECK(pieces[0].model.mesh.vertices.size() == 4);
    CHECK(pieces[0].model.mesh.indices.size() == 6);
}

TEST_CASE("a piece gets its own bounds, which is what the split buys")
{
    Model model = triangles(3);
    model.submeshNames = {"A", "B", "C"};

    const std::vector<ModelPiece> pieces = splitByPrimitive(model);

    REQUIRE(pieces.size() == 3);
    // The third triangle sits at y = 2 and nowhere near y = 0, so its box says
    // so. The whole model's box would enclose all three -- which is the
    // conservative whole-mesh cull the renderer apologises for.
    CHECK(pieces[2].model.mesh.bounds.min.y == 2.0f);
    CHECK(pieces[2].model.mesh.bounds.max.y == 2.0f);
    CHECK(pieces[0].model.mesh.bounds.min.y == 0.0f);
    // The submesh's box agrees with the model's, because for one piece they are
    // the same box.
    CHECK(pieces[2].model.mesh.submeshes[0].bounds.min.y == 2.0f);
}

TEST_CASE("a node that produced several pieces names them by material")
{
    // One node, three primitives. Its own name cannot tell them apart, so the
    // material's does -- which is what distinguishes a body from a mane inside
    // one node.
    Model model = triangles(3);
    model.submeshNames = {"Horse", "Horse", "Horse"};

    const std::vector<ModelPiece> pieces = splitByPrimitive(model);

    REQUIRE(pieces.size() == 3);
    CHECK(pieces[0].name == "Material0");
    CHECK(pieces[1].name == "Material1");
    CHECK(pieces[2].name == "Material2");
}

TEST_CASE("a collision is resolved by an ordinal, in document order")
{
    // Two nodes with one primitive each, both called Wheel, both using one
    // material. Neither rule distinguishes them, so the suffix does -- and it
    // has to be the SAME suffix on every machine, because the name becomes both
    // the URN fragment and the instance's name.
    Model model = triangles(2);
    model.materials[0].name = "Rubber";
    model.materials[1].name = "Rubber";
    model.submeshNames = {"Wheel", "Wheel"};

    const std::vector<ModelPiece> pieces = splitByPrimitive(model);

    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0].name == "Rubber");
    CHECK(pieces[1].name == "Rubber_2");

    // Run it again: the same answer, which is the property that matters.
    const std::vector<ModelPiece> again = splitByPrimitive(model);
    REQUIRE(again.size() == 2);
    CHECK(again[0].name == pieces[0].name);
    CHECK(again[1].name == pieces[1].name);
}

TEST_CASE("a piece nothing named gets an ordinal")
{
    Model model = triangles(2);
    model.materials[0].name.clear();
    model.materials[1].name.clear();
    // Whitespace is not a name. An exporter that writes it has said nothing.
    model.submeshNames = {"", "   "};

    const std::vector<ModelPiece> pieces = splitByPrimitive(model);

    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0].name == "Part1");
    CHECK(pieces[1].name == "Part2");
}

TEST_CASE("a skinned model is one piece, however many primitives it has")
{
    // **A decision, not an omission.** `AnimationPlayer`'s contract is "parent
    // it to the MeshPart whose skeleton", a skeleton belongs to one mesh, and a
    // 677-joint rig duplicated into every piece would be the rig several times
    // over in memory and several palettes uploaded per frame for one answer.
    Model model = triangles(4);
    model.submeshNames = {"Body", "Mane", "Saddle", "Tail"};
    model.joints.push_back(Joint{});
    model.skin.assign(model.mesh.vertices.size(), SkinVertex{});

    const std::vector<ModelPiece> pieces = splitByPrimitive(model);

    REQUIRE(pieces.size() == 1);
    CHECK(pieces[0].name == "Body");
    CHECK(pieces[0].model.mesh.submeshes.size() == 4);
    CHECK(pieces[0].model.joints.size() == 1);
}

TEST_CASE("a model with nothing in it splits into nothing")
{
    const std::vector<ModelPiece> pieces = splitByPrimitive(Model{});
    CHECK(pieces.empty());
}
