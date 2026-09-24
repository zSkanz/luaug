// Material assets (ADR 0090): the file, variants, the library.
#include "luaug/asset/material.h"

#include <doctest/doctest.h>
#include <map>
#include <string>

using namespace luaug;
using asset::fieldBit;
using asset::MaterialAsset;
using asset::MaterialField;

namespace {

// A base with a value in every field that is not the default, so a round trip
// that dropped one would show.
MaterialAsset brick()
{
    MaterialAsset out;
    out.instanceParameters = fieldBit(MaterialField::Color);
    out.properties.color = core::Color3{0.8f, 0.3f, 0.2f};
    out.properties.roughness = 0.9f;
    out.properties.colorMap = "asset://textures/brick.png";
    out.properties.normalMap = "asset://textures/brick_n.png";
    out.properties.alphaMode = 1;
    out.properties.alphaCutoff = 0.25f;
    out.written = asset::AllMaterialFields;
    return out;
}

} // namespace

TEST_CASE("a base material writes every field, in a fixed order, and reads back to the same bytes")
{
    const std::string text = asset::writeMaterialAsset(brick());
    CHECK(text == R"({
  "format": "luaug-material",
  "version": 1,
  "parent": "",
  "instanceParameters": ["Color"],
  "properties": {
    "Color": [0.8, 0.3, 0.2],
    "Transparency": 0,
    "ColorMap": "asset://textures/brick.png",
    "NormalMap": "asset://textures/brick_n.png",
    "MetallicRoughnessMap": "",
    "Emissive": [0, 0, 0],
    "EmissiveMap": "",
    "Metalness": 0,
    "Roughness": 0.9,
    "NormalScale": 1,
    "AlphaMode": "Mask",
    "AlphaCutoff": 0.25,
    "DoubleSided": false
  }
}
)");

    asset::MaterialReadNotes notes;
    const std::optional<MaterialAsset> read = asset::readMaterialAsset(text, &notes);
    REQUIRE(read.has_value());
    CHECK(notes.unknownFields.empty());
    CHECK(notes.malformedFields.empty());
    CHECK(*read == brick());
    CHECK(asset::writeMaterialAsset(*read) == text);
}

TEST_CASE("a variant writes only what it overrides, and inherits the rest from its parent")
{
    MaterialAsset mossy;
    mossy.parent = "asset://materials/brick.material.json";
    mossy.properties.color = core::Color3{0.3f, 0.5f, 0.2f};
    mossy.written = fieldBit(MaterialField::Color);
    mossy.instanceParameters = fieldBit(MaterialField::Roughness);

    const std::string text = asset::writeMaterialAsset(mossy);
    CHECK(text.find("\"Roughness\": 0.9") == std::string::npos);
    CHECK(text.find("\"ColorMap\"") == std::string::npos);
    const std::optional<MaterialAsset> read = asset::readMaterialAsset(text);
    REQUIRE(read.has_value());
    CHECK(*read == mossy);

    const MaterialAsset base = brick();
    const std::map<std::string, MaterialAsset> files{{"asset://materials/brick.material.json", base},
                                                     {"asset://materials/mossy.material.json", mossy}};
    const asset::MaterialLookup lookup = [&](std::string_view urn) -> const MaterialAsset* {
        const auto found = files.find(std::string(urn));
        return found == files.end() ? nullptr : &found->second;
    };
    const asset::ResolvedMaterial resolved = asset::resolveMaterial("asset://materials/mossy.material.json", lookup);
    CHECK(resolved.properties.color == core::Color3{0.3f, 0.5f, 0.2f});
    // Inherited.
    CHECK(static_cast<double>(resolved.properties.roughness) == doctest::Approx(0.9));
    CHECK(resolved.properties.colorMap == "asset://textures/brick.png");
    // The parent's declarations and its own.
    CHECK(resolved.instanceParameters == (fieldBit(MaterialField::Color) | fieldBit(MaterialField::Roughness)));
}

TEST_CASE("an authored material declares only what its file declares; the default declares Color and Transparency")
{
    CHECK(asset::defaultMaterial().instanceParameters ==
          (fieldBit(MaterialField::Color) | fieldBit(MaterialField::Transparency)));
    CHECK(static_cast<double>(asset::defaultMaterial().properties.roughness) == doctest::Approx(0.7));
    CHECK(asset::defaultMaterial().properties.color == core::Color3{1.0f, 1.0f, 1.0f});

    MaterialAsset plain;
    plain.written = asset::AllMaterialFields;
    const asset::MaterialLookup lookup = [&](std::string_view) { return &plain; };
    CHECK(asset::resolveMaterial("asset://materials/plain.material.json", lookup).instanceParameters == 0);
}

TEST_CASE("a cycle is refused, resolves to the default, and names the file that closed it")
{
    std::map<std::string, MaterialAsset> files;
    MaterialAsset a;
    a.parent = "asset://b.material.json";
    a.properties.color = core::Color3{1.0f, 0.0f, 0.0f};
    a.written = fieldBit(MaterialField::Color);
    MaterialAsset b;
    b.parent = "asset://a.material.json";
    files["asset://a.material.json"] = a;
    files["asset://b.material.json"] = b;
    const asset::MaterialLookup lookup = [&](std::string_view urn) -> const MaterialAsset* {
        const auto found = files.find(std::string(urn));
        return found == files.end() ? nullptr : &found->second;
    };

    asset::MaterialResolveNotes notes;
    const asset::ResolvedMaterial resolved = asset::resolveMaterial("asset://a.material.json", lookup, &notes);
    CHECK(resolved.properties == asset::defaultMaterial().properties);
    CHECK(resolved.instanceParameters == asset::defaultMaterial().instanceParameters);
    CHECK(notes.cycleClosedBy == "asset://b.material.json");

    // A material that is its own parent is the shortest loop.
    MaterialAsset self;
    self.parent = "asset://self.material.json";
    files["asset://self.material.json"] = self;
    asset::MaterialResolveNotes selfNotes;
    (void)asset::resolveMaterial("asset://self.material.json", lookup, &selfNotes);
    CHECK(selfNotes.cycleClosedBy == "asset://self.material.json");
}

TEST_CASE("an unknown field is reported and not fatal; a wrong-shaped one reads as absent")
{
    const std::string text = R"({
  "format": "luaug-material",
  "version": 1,
  "parent": "",
  "shader": "asset://shaders/toon.hlsl",
  "instanceParameters": ["Color", "ColorMap", "Sparkle"],
  "properties": { "Color": [0, 1, 0], "Glow": 3, "Roughness": "very" }
})";
    asset::MaterialReadNotes notes;
    const std::optional<MaterialAsset> read = asset::readMaterialAsset(text, &notes);
    REQUIRE(read.has_value());
    CHECK(read->properties.color == core::Color3{0.0f, 1.0f, 0.0f});
    CHECK(static_cast<double>(read->properties.roughness) == doctest::Approx(0.7));
    CHECK(read->written == fieldBit(MaterialField::Color));
    // A map is not declarable, and a name that is not a field is not a field.
    CHECK(read->instanceParameters == fieldBit(MaterialField::Color));
    CHECK(notes.unknownFields.size() == 4);
    CHECK(notes.malformedFields.size() == 1);

    // What is not a material at all is refused, with a reason.
    std::string error;
    CHECK_FALSE(asset::readMaterialAsset(R"({"format":"luaug-scene","version":1})", nullptr, &error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(asset::readMaterialAsset("{ not json", nullptr, &error).has_value());
    CHECK_FALSE(asset::readMaterialAsset(R"({"format":"luaug-material","version":2})").has_value());
}

TEST_CASE("a missing parent is reported, and the variant's own fields still apply")
{
    MaterialAsset orphan;
    orphan.parent = "asset://gone.material.json";
    orphan.properties.metalness = 1.0f;
    orphan.written = fieldBit(MaterialField::Metalness);
    const asset::MaterialLookup lookup = [&](std::string_view urn) -> const MaterialAsset* {
        return urn == "asset://orphan.material.json" ? &orphan : nullptr;
    };
    asset::MaterialResolveNotes notes;
    const asset::ResolvedMaterial resolved = asset::resolveMaterial("asset://orphan.material.json", lookup, &notes);
    CHECK(static_cast<double>(resolved.properties.metalness) == doctest::Approx(1.0));
    CHECK(static_cast<double>(resolved.properties.roughness) == doctest::Approx(0.7));
    CHECK(notes.missingParentOf == "asset://orphan.material.json");
    CHECK(notes.missingParent == "asset://gone.material.json");
}

TEST_CASE("the library loads once, resolves once, and forgets a changed file")
{
    int reads = 0;
    core::Color3 onDisk{1.0f, 0.0f, 0.0f};
    asset::MaterialLibrary library(
        [&](std::string_view urn, asset::MaterialReadNotes&) -> std::optional<MaterialAsset> {
            ++reads;
            if (urn != "asset://red.material.json")
                return std::nullopt;
            MaterialAsset material;
            material.properties.color = onDisk;
            material.written = asset::AllMaterialFields;
            return material;
        });

    CHECK(library.resolve("asset://red.material.json").properties.color == core::Color3{1.0f, 0.0f, 0.0f});
    CHECK(library.resolve("asset://red.material.json").properties.color == core::Color3{1.0f, 0.0f, 0.0f});
    CHECK(reads == 1);

    // Missing is the default, and remembered as missing.
    CHECK(library.resolve("asset://none.material.json").properties == asset::defaultMaterial().properties);
    CHECK_FALSE(library.exists("asset://none.material.json"));
    CHECK(reads == 2);

    const core::u64 before = library.revision();
    onDisk = core::Color3{0.0f, 0.0f, 1.0f};
    library.forget("asset://red.material.json");
    CHECK(library.revision() != before);
    CHECK(library.resolve("asset://red.material.json").properties.color == core::Color3{0.0f, 0.0f, 1.0f});

    // The empty URN is the default without asking the source.
    const int reads2 = reads;
    CHECK(library.resolve("").instanceParameters == asset::DefaultMaterialParameters);
    CHECK(reads == reads2);
}

TEST_CASE("a part's overrides apply only where the material declares them, and clearing one is exact")
{
    asset::MaterialOverrides overrides;
    asset::MaterialProperties values;
    values.color = core::Color3{0.1f, 0.2f, 0.3f};
    values.roughness = 0.1f;
    CHECK(asset::setOverride(overrides, MaterialField::Color, values));
    CHECK(asset::setOverride(overrides, MaterialField::Roughness, values));
    // A map is not a parameter.
    CHECK_FALSE(asset::setOverride(overrides, MaterialField::ColorMap, values));

    asset::MaterialProperties drawn;
    asset::applyOverrides(overrides, fieldBit(MaterialField::Color), drawn);
    CHECK(drawn.color == core::Color3{0.1f, 0.2f, 0.3f});
    // Kept on the part, and ignored by a material that does not declare it.
    CHECK(static_cast<double>(drawn.roughness) == doctest::Approx(0.7));
    CHECK(overrides.has(MaterialField::Roughness));

    asset::clearOverride(overrides, MaterialField::Roughness);
    asset::clearOverride(overrides, MaterialField::Color);
    CHECK(overrides == asset::MaterialOverrides{});
}
