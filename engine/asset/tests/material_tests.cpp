#include "luaug/asset/material.h"
#include "luaug/core/i18n.h"

#include <doctest/doctest.h>
#include <string>

using namespace luaug::asset;
using luaug::core::engineCatalog;

namespace {

void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

[[nodiscard]] MaterialAsset horseBody()
{
    MaterialAsset material;
    material.name = "Body";
    material.baseColorFactor = Color3{0.8f, 0.6f, 0.4f};
    material.baseColorAlpha = 0.5f;
    material.metallicFactor = 0.25f;
    material.roughnessFactor = 0.75f;
    material.emissiveFactor = Color3{0.1f, 0.0f, 0.2f};
    material.normalScale = 2.0f;
    material.alphaMode = AlphaMode::Mask;
    material.alphaCutoff = 0.125f;
    material.doubleSided = true;
    material.baseColor.content = "asset://models/horse/textures/body.png";
    material.normal.content = "asset://models/horse/textures/body_n.png";
    material.normal.uvSet = 1;
    material.emissive.content = "asset://models/horse/textures/eyes.png";
    return material;
}

} // namespace

TEST_CASE("a material round-trips through its own text")
{
    seedRealCatalog();
    const MaterialAsset written = horseBody();

    MaterialAsset read;
    REQUIRE_FALSE(readMaterial(writeMaterial(written), "body.material.json", read).has_value());

    // Every field, by whole-struct equality rather than field by field: a test
    // that names the fields is a test that stops covering the one added next.
    CHECK(read == written);

    // And the TEXT is a pure function of the struct, which is what lets a
    // material be diffed and content-hashed.
    CHECK(writeMaterial(read) == writeMaterial(written));
}

TEST_CASE("an unused texture slot survives the round trip as unused")
{
    seedRealCatalog();
    MaterialAsset written;
    written.name = "Plain";
    written.baseColor.content = "asset://t.png";

    MaterialAsset read;
    REQUIRE_FALSE(readMaterial(writeMaterial(written), "plain.material.json", read).has_value());

    CHECK(read.baseColor.present());
    CHECK_FALSE(read.normal.present());
    CHECK_FALSE(read.metallicRoughness.present());
    CHECK_FALSE(read.emissive.present());
    CHECK(read == written);
}

TEST_CASE("a trimmed material loads, and every absence is the default")
{
    seedRealCatalog();
    // What somebody would write by hand, and what an older build's file looks
    // like to a newer one. Both must load rather than be refused.
    MaterialAsset read;
    REQUIRE_FALSE(
        readMaterial(R"({"format":"luaug-material","roughnessFactor":0.5})", "hand.material.json", read).has_value());

    CHECK(read.roughnessFactor == 0.5f);
    const MaterialAsset defaults;
    CHECK(read.shader == defaults.shader);
    CHECK(read.metallicFactor == defaults.metallicFactor);
    CHECK(read.alphaMode == defaults.alphaMode);
    CHECK_FALSE(read.baseColor.present());

    // An alpha mode this build does not know is the default too, for the same
    // reason: losing one property beats failing to open.
    MaterialAsset newer;
    REQUIRE_FALSE(
        readMaterial(R"({"format":"luaug-material","alphaMode":"dither"})", "newer.material.json", newer).has_value());
    CHECK(newer.alphaMode == AlphaMode::Opaque);
}

TEST_CASE("a document that is not a material is refused by name")
{
    seedRealCatalog();
    MaterialAsset out;

    // The case this check exists for: a `.json` that is really a scene must say
    // so rather than load as a white material with every default.
    CHECK(readMaterial(R"({"root":{"class":"Workspace"}})", "main.scene.json", out).has_value());
    CHECK(readMaterial("not json at all", "junk.material.json", out).has_value());
    CHECK(readMaterial("", "empty.material.json", out).has_value());

    // And a refusal leaves the output alone, so a caller reusing one struct
    // across a folder does not inherit half of a file that failed.
    CHECK(out == MaterialAsset{});
}

TEST_CASE("every alpha mode survives its own name")
{
    seedRealCatalog();
    for (const AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend}) {
        MaterialAsset written;
        written.alphaMode = mode;
        MaterialAsset read;
        REQUIRE_FALSE(readMaterial(writeMaterial(written), "m.material.json", read).has_value());
        CHECK(read.alphaMode == mode);
    }
}
