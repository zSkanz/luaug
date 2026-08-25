#include "luaug/asset/model_split.h"

#include <unordered_map>
#include <unordered_set>

namespace luaug::asset {
namespace {

// A name a file can be trusted to have given, or empty. An exporter that writes
// whitespace, or a name made only of separators, has said nothing.
[[nodiscard]] std::string cleaned(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

} // namespace

std::vector<ModelPiece> splitByPrimitive(const Model& model)
{
    std::vector<ModelPiece> pieces;

    const std::size_t count = model.mesh.submeshes.size();
    if (count == 0)
        return pieces;

    // A skinned file is one piece. See the header: a skeleton belongs to one
    // mesh, and duplicating a rig into every piece is the rig several times over
    // for one answer.
    if (!model.joints.empty() && !model.skin.empty()) {
        ModelPiece whole;
        whole.name = count > 0 && !model.submeshNames.empty() ? cleaned(model.submeshNames[0]) : std::string{};
        whole.model = model;
        pieces.push_back(std::move(whole));
        return pieces;
    }

    // How many pieces each node name is about to produce. A node that produces
    // ONE piece keeps its own name; a node that produces several would give them
    // all the same name, so those fall through to the material's.
    std::unordered_map<std::string, std::size_t> perName;
    for (std::size_t index = 0; index < count; ++index) {
        const std::string name = index < model.submeshNames.size() ? cleaned(model.submeshNames[index]) : std::string{};
        if (!name.empty())
            perName[name] += 1;
    }

    // Every name already handed out, so a second piece asking for one gets
    // `_2` -- in DOCUMENT ORDER, which is what makes the answer the same on
    // every machine and across every re-import.
    std::unordered_set<std::string> taken;

    pieces.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const Submesh& submesh = model.mesh.submeshes[index];

        std::string name = index < model.submeshNames.size() ? cleaned(model.submeshNames[index]) : std::string{};
        if (!name.empty() && perName[name] > 1) {
            // The node produced several pieces, so its own name cannot tell them
            // apart. The material's name is what distinguishes a body from a
            // mane inside one node.
            const std::string material =
                submesh.material < model.materials.size() ? cleaned(model.materials[submesh.material].name) : "";
            if (!material.empty())
                name = material;
        }
        if (name.empty())
            name = "Part" + std::to_string(index + 1);

        std::string unique = name;
        for (std::size_t suffix = 2; taken.count(unique) != 0; ++suffix)
            unique = name + "_" + std::to_string(suffix);
        taken.insert(unique);

        // The piece's own geometry: this submesh's indices, and only the
        // vertices they reach. A piece that carried the whole model's vertex
        // buffer would be the file's memory once per piece, and the simplifier
        // would then be handed vertices no index in the piece names.
        ModelPiece piece;
        piece.name = unique;
        piece.model.materials = model.materials;
        piece.model.images = model.images;

        std::unordered_map<u32, u32> remap;
        remap.reserve(submesh.indexCount);
        piece.model.mesh.indices.reserve(submesh.indexCount);
        for (u32 offset = 0; offset < submesh.indexCount; ++offset) {
            const u32 source = model.mesh.indices[submesh.firstIndex + offset];
            const auto found = remap.find(source);
            if (found != remap.end()) {
                piece.model.mesh.indices.push_back(found->second);
                continue;
            }
            const auto fresh = static_cast<u32>(piece.model.mesh.vertices.size());
            // **First-use order, not sorted.** The remap has to be a pure
            // function of the index stream, and an unordered container's
            // iteration is not one (R10) -- so the new index is assigned as the
            // stream reaches it and the container is only ever asked "have I
            // seen this", never "what do you hold".
            remap.emplace(source, fresh);
            piece.model.mesh.vertices.push_back(model.mesh.vertices[source]);
            piece.model.mesh.indices.push_back(fresh);
        }

        Submesh only;
        only.firstIndex = 0;
        only.indexCount = static_cast<u32>(piece.model.mesh.indices.size());
        only.material = submesh.material;
        only.bounds = submesh.bounds;
        piece.model.mesh.submeshes.push_back(only);
        piece.model.submeshNames.push_back(unique);

        // Its own bounds, from its own vertices. This is the per-piece cull the
        // whole split buys: the model's bounds enclose the horse to cull its
        // mane. An empty box starts at inverted infinity, so expanding over no
        // vertices leaves it empty rather than at the origin.
        for (const Vertex& vertex : piece.model.mesh.vertices)
            core::expand(piece.model.mesh.bounds, vertex.position);
        piece.model.mesh.submeshes[0].bounds = piece.model.mesh.bounds;

        pieces.push_back(std::move(piece));
    }

    return pieces;
}

} // namespace luaug::asset
