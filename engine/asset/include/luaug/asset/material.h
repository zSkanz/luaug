// A material as an author writes it: `horse.Body.material.json`, beside the
// file it came from.
//
// **A parameter block, not a shader graph.** Every field here is a uniform or a
// texture the built-in PBR shader already consumes, and `shader` is a name
// rather than an enum so a material that wants vertex-displaced water names a
// different one (M4 brief, Decision 6). Nodes, subgraphs and a compiler are a
// different feature, and saying so here is cheaper than saying it in a review.
//
// It is `MaterialDef` (`model.h`) with the four `TextureRef`s replaced by
// content URNs. That difference is the whole point of the file existing: a
// `TextureRef` is an index into one model's decoded image list, which is a name
// only that model can read, and an author needs to point two parts at one
// texture and to swap a texture without re-importing anything.
//
// **The URNs name SOURCE images** -- the `.png` the artist shipped, not a
// compiled blob. An authored file has to survive its cache being deleted, and
// naming a hash would make every material stale the moment anything recompiled.
// The compiler resolves them; the game ships the result.
//
// The text is one line, like every other authored format in this repo
// (`.scene.json`), and every key is written in a fixed order, so the bytes are a
// pure function of the struct and a diff shows what changed rather than how it
// was serialised.
#pragma once

#include "luaug/asset/model.h"
#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <optional>
#include <string>
#include <string_view>

namespace luaug::asset {

// One slot, pointing at a source image. An empty `content` is "this slot is not
// used", which is what `TextureRef::Missing` means on the other side.
struct MaterialTexture
{
    std::string content;
    u32 uvSet = 0;

    [[nodiscard]] bool present() const noexcept { return !content.empty(); }
    [[nodiscard]] bool operator==(const MaterialTexture&) const = default;
};

struct MaterialAsset
{
    std::string name;
    std::string shader = "pbr";

    Color3 baseColorFactor{1.0f, 1.0f, 1.0f};
    f32 baseColorAlpha = 1.0f;
    f32 metallicFactor = 1.0f;
    f32 roughnessFactor = 1.0f;
    Color3 emissiveFactor{0.0f, 0.0f, 0.0f};
    f32 normalScale = 1.0f;

    AlphaMode alphaMode = AlphaMode::Opaque;
    f32 alphaCutoff = 0.5f;
    bool doubleSided = false;

    MaterialTexture baseColor;
    MaterialTexture normal;
    // Occlusion, roughness and metalness in one texture's R, G and B. One slot,
    // because they are one image.
    MaterialTexture metallicRoughness;
    MaterialTexture emissive;

    [[nodiscard]] bool operator==(const MaterialAsset&) const = default;
};

// The extension an authored material takes. A double extension rather than
// `.mat`, so that the name says what reads it and a text editor already knows
// how to colour it.
inline constexpr std::string_view MaterialExtension = ".material.json";

// Stable text, in fixed key order.
[[nodiscard]] std::string writeMaterial(const MaterialAsset& material);

// **Every field is optional and every absence is the struct's own default.** A
// material written by an older build must still load, and a material an author
// trimmed by hand to three lines is a legitimate file rather than a broken one.
// What is refused: text that is not JSON, and a document whose `format` is not
// ours -- because a `.json` that happens to be a scene should say so rather than
// load as a white material.
//
// `sourceName` appears in the diagnostic and is never read as a path.
[[nodiscard]] std::optional<core::EngineError> readMaterial(std::string_view text, std::string_view sourceName,
                                                            MaterialAsset& out);

} // namespace luaug::asset
