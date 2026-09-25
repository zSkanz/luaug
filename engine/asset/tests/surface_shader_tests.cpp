// A surface shader read and wrapped (ADR 0091): what the engine learns from the
// source, and the HLSL it compiles around it. No compiler here -- the wrappers
// are compiled with the engine's own shaders, which is where a contract that no
// longer compiles fails the build.
#include "luaug/asset/surface_shader.h"

#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace luaug;
using namespace luaug::asset;

namespace {

constexpr std::string_view Ocean = R"(#include "luaug/surface.hlsli"

// A comment that mentions LUAUG_PARAM(float, NotAParam, 1) is not a parameter.
LUAUG_PARAM(float, WaveHeight, 0.5, range(0, 4))
LUAUG_PARAM(float3, Deep, float3(0.0, 0.1, 0.2), colour)
LUAUG_PARAM(float, Speed, 1.0f)
LUAUG_PARAM(bool, Foamy, true, toggle)
LUAUG_PARAM(float4, Tint, float4(1, 1, 1, 1))
LUAUG_TEXTURE(Foam)
LUAUG_TEXTURE(Ripples, normal)

void surfaceVertex(inout SurfaceVertex vertex, SurfaceInputs inputs)
{
    vertex.Position.y += sin(inputs.Time * Speed + vertex.Position.x) * WaveHeight;
}

void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)
{
    surface.BaseColor = Deep + LUAUG_SAMPLE(Foam, inputs.Uv0).rgb;
}
)";

} // namespace

TEST_CASE("a surface's parameters are read with their types, defaults and annotations, packed as a cbuffer packs them")
{
    const SurfaceReflection reflection = reflectSurface(Ocean);
    REQUIRE(reflection.ok());
    REQUIRE(reflection.params.size() == 5);
    CHECK(reflection.hasVertex);
    CHECK(reflection.hasFragment);

    const SurfaceParam& height = reflection.params[0];
    CHECK(height.name == "WaveHeight");
    CHECK(height.annotation == SurfaceAnnotation::Range);
    CHECK(static_cast<double>(height.maximum) == doctest::Approx(4.0));
    CHECK(height.offset == 16);
    CHECK(height.line == 4);

    // A float3 fits after one float in a row of sixteen -- four and twelve.
    const SurfaceParam& deep = *reflection.param("Deep");
    CHECK(deep.offset == 20);
    CHECK(deep.annotation == SurfaceAnnotation::Colour);
    CHECK(static_cast<double>(deep.value[2]) == doctest::Approx(0.2));
    // The row is full, so the next float starts one.
    CHECK(reflection.param("Speed")->offset == 32);
    CHECK(reflection.param("Foamy")->offset == 36);
    CHECK(static_cast<double>(reflection.param("Foamy")->value[0]) == doctest::Approx(1.0));
    // A float4 never straddles: after 40 it goes to 48.
    CHECK(reflection.param("Tint")->offset == 48);
    CHECK(reflection.blockBytes == 64);

    REQUIRE(reflection.textures.size() == 2);
    CHECK(reflection.textures[1].fallback == SurfaceTextureDefault::Normal);
    CHECK(reflection.param("NotAParam") == nullptr);
}

TEST_CASE("what a surface must not write is reported by line, and nothing throws")
{
    const SurfaceReflection reflection = reflectSurface(R"(LUAUG_PARAM(float, Fine, 1)
Texture2D Mine : register(t0);
LUAUG_PARAM(half, Wrong, 1)
LUAUG_PARAM(float, Fine, 2)
LUAUG_PARAM(float2, Short, float2(1))
LUAUG_PARAM(float, Odd, 1, colour)
LUAUG_PARAM(float, LuaugClock, 1)
float4 FragmentMain() : SV_Target0 { return 0; }
)");
    CHECK_FALSE(reflection.ok());
    const auto has = [&](core::u32 line, std::string_view key) {
        for (const SurfaceDiagnostic& diagnostic : reflection.errors) {
            if (diagnostic.line == line && diagnostic.key == key)
                return true;
        }
        return false;
    };
    CHECK(has(2, "asset.err.surface_forbidden"));
    CHECK(has(3, "asset.err.surface_param_type"));
    CHECK(has(4, "asset.err.surface_param_name"));
    CHECK(has(5, "asset.err.surface_param_default"));
    CHECK(has(6, "asset.err.surface_param_annotation"));
    CHECK(has(7, "asset.err.surface_param_name"));
    CHECK(has(8, "asset.err.surface_forbidden"));
    // Only what was declared well survives.
    REQUIRE(reflection.params.size() == 1);
    CHECK(reflection.params[0].name == "Fine");
    CHECK_FALSE(reflection.hasVertex);
    CHECK_FALSE(reflection.hasFragment);

    CHECK(reflectSurface("").ok());
}

TEST_CASE("a wrapper declares the block and the textures where the renderer binds them, and includes the user's file")
{
    const SurfaceReflection reflection = reflectSurface(Ocean);
    const std::string vertex =
        surfaceWrapper(reflection, SurfaceVariant::Forward, SurfaceStage::Vertex, "ocean.surface.hlsl");
    CHECK(vertex.find("cbuffer LuaugSurfaceBlock : register(b1, space1)") != std::string::npos);
    CHECK(vertex.find("Texture2D Foam : register(t0, space0)") != std::string::npos);
    CHECK(vertex.find("#include \"ocean.surface.hlsl\"") != std::string::npos);
    CHECK(vertex.find("VertexMain") != std::string::npos);

    const std::string fragment =
        surfaceWrapper(reflection, SurfaceVariant::Forward, SurfaceStage::Fragment, "ocean.surface.hlsl");
    CHECK(fragment.find("register(b2, space3)") != std::string::npos);
    CHECK(fragment.find("Texture2D Ripples : register(t14, space2)") != std::string::npos);
    CHECK(fragment.find("lightSurface") != std::string::npos);

    const std::string depth =
        surfaceWrapper(reflection, SurfaceVariant::DepthInstanced, SurfaceStage::Vertex, "ocean.surface.hlsl");
    CHECK(depth.find("#define LUAUG_SURFACE_INSTANCED") != std::string::npos);
    CHECK(depth.find("LightViewProjection") != std::string::npos);

    // A surface that leaves a function out gets an empty one.
    const SurfaceReflection bare = reflectSurface("LUAUG_PARAM(float, A, 1)\n");
    const std::string stubbed = surfaceWrapper(bare, SurfaceVariant::Forward, SurfaceStage::Fragment, "bare.hlsl");
    CHECK(stubbed.find("void surfaceVertex(inout SurfaceVertex vertex, SurfaceInputs inputs)") != std::string::npos);
    CHECK(stubbed.find("void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)") != std::string::npos);

    CHECK(surfaceResourceCounts(reflection, SurfaceVariant::Forward, SurfaceStage::Fragment).samplers == 15);
    CHECK(surfaceResourceCounts(reflection, SurfaceVariant::Forward, SurfaceStage::Fragment).uniformBuffers == 3);
    CHECK(surfaceResourceCounts(reflection, SurfaceVariant::Depth, SurfaceStage::Vertex).samplers == 2);
}

TEST_CASE("a parameter's value is written where its offset says, in its own representation")
{
    const SurfaceReflection reflection = reflectSurface(Ocean);
    std::vector<core::u8> block(reflection.blockBytes, 0);
    const core::f32 deep[3] = {0.25f, 0.5f, 0.75f};
    writeSurfaceParam(*reflection.param("Deep"), deep, block);
    const core::f32 off[1] = {0.0f};
    writeSurfaceParam(*reflection.param("Foamy"), off, block);
    core::f32 read[3]{};
    std::memcpy(read, block.data() + 20, sizeof(read));
    CHECK(read[0] == 0.25f);
    CHECK(read[2] == 0.75f);
    core::u32 flag = 7;
    std::memcpy(&flag, block.data() + 36, 4);
    CHECK(flag == 0u);
}
