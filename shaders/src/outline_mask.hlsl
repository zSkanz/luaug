// The editor's selection mask: which pixels belong to a selected object.
//
// **A silhouette rather than a box** (E2). The wire box the first editor drew
// says where a thing's BOUNDS are, which for anything that is not a cube is a
// shape the object does not have -- and for a tree, a rock or a character it is
// a box floating in the air around the thing you clicked. What a person means
// by "show me what is selected" is the outline of the object itself, which is
// what a mask plus a dilate produces and what no amount of wireframe can.
//
// This is the first half: draw every selected object, flat, into a single-
// channel target. One means selected. The second half, `outline_composite`,
// turns the edge of that into a line.
//
// **The vertex stage is `shadow_depth`'s, deliberately.** The mask wants
// exactly what a depth-only pass wants -- position through a view-projection
// and a model matrix, nothing else -- and pairing this fragment with
// `shadow_depth`, `shadow_instanced` and `shadow_skinned` gives the static, the
// batched and the skinned variants for one file rather than three. The vertex
// entry below is what makes THIS file's own static pipeline, and it is the same
// two lines for the same reason.
//
// Register spaces are fixed per stage by SDL_GPU and vertex inputs use TEXCOORDn
// because that is what SDL_shadercross maps to SPIR-V input locations.

#define LUAUG_UNIFORMS_SHADOW
#include "luaug_pbr.hlsli"

struct VertexInput
{
    float3 Position : TEXCOORD0;
};

struct Interpolants
{
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;
    output.Position = mul(LightViewProjection, mul(ShadowModel, float4(input.Position, 1.0f)));
    return output;
}

// One channel, and the value carries no information beyond "here". Everything
// the outline needs to know is WHERE the ones stop, which is the next pass's
// question.
float FragmentMain() : SV_Target0
{
    return 1.0f;
}
