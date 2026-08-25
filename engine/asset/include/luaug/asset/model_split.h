// One imported file, cut into the pieces a person selects.
//
// **A model arrives as one opaque mesh, and that is the wrong shape.** Five
// materials become five submeshes of one `MeshPart`: nothing to select, nothing
// to give a material to, and nothing for a `Model.Scale` to scale. What an
// author has in the file is a body, a mane and a saddle; what the engine had was
// "horse".
//
// So the split happens between `importGltf` and `compileMesh` -- a pure
// operation on an `asset::Model`, needing no codec and no GPU, which is why it
// lives here rather than in the compiler. The editor and `assetc` call the same
// function, and the `.lmesh` format does not change: each piece is compiled the
// way a whole model always was.
//
// **Draw cost is unchanged.** The renderer already emits one draw per section,
// so two parts with one section each are the same two draws two sections of one
// part were. What is gained is per-piece bounds -- the "conservative whole-mesh
// cull" `render_world.cpp` apologises for becomes a real per-piece cull -- and a
// name, a material and a transform per piece.
#pragma once

#include "luaug/asset/model.h"

#include <string>
#include <vector>

namespace luaug::asset {

// One piece of a split model: a whole `Model` in its own right, plus the name it
// should wear.
struct ModelPiece
{
    std::string name;
    Model model;
};

// `model` cut into one piece per submesh.
//
// **A SKINNED model is not split**, and that is a decision rather than an
// omission. `AnimationPlayer`'s contract is "parent it to the `MeshPart` whose
// skeleton" -- a skeleton belongs to one mesh, and a 677-joint rig duplicated
// into every piece would be the rig several times over in memory and several
// palettes uploaded per frame for one answer. A skinned file comes back as one
// piece wearing the file's own name.
//
// Naming, in order, because a person has to be able to find a piece in an
// Explorer with forty of them:
//
//   1. the node's name, when the importer recorded one (`Model::submeshNames`);
//   2. the material's name, when a node produced several pieces and its own name
//      would therefore be ambiguous;
//   3. an ordinal, when neither says anything.
//
// **Collisions are resolved by `_2`, `_3` in document order**, never by a hash
// or a random suffix: the name becomes the URN fragment AND the instance's name,
// so it has to be the same on every machine and across every re-import, or a
// scene that names a piece stops finding it.
[[nodiscard]] std::vector<ModelPiece> splitByPrimitive(const Model& model);

} // namespace luaug::asset
