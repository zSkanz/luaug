// What the look's screen passes share (ADR 0096): their uniform blocks, and
// the circle of confusion depth of field is built from.
//
// **Kept out of `luaug_pbr.hlsli` on purpose.** Every pipeline the engine had
// before these effects includes that file, and a world with none of them must
// draw through exactly the shaders it always drew through; a block added there
// would be text every one of them compiles. Only the look's own passes include
// this one.
#ifndef LUAUG_LOOK_HLSLI
#define LUAUG_LOOK_HLSLI

#if defined(LUAUG_UNIFORMS_FOCUS)
// `render::GpuLookFocusUniforms`, 48 bytes.
cbuffer GpuLookFocusUniforms : register(b0, space3)
{
    // x the distance that is sharpest, y how far either side stays sharp, both
    // in metres; z how soft the near side becomes, w the far side, 0 to 1.
    float4 FocusBand;
    // x near plane, y far plane, z the widest circle in HALF-resolution texels,
    // w the same in full-resolution pixels.
    float4 FocusLens;
    // xy one full-resolution texel, zw one half-resolution texel.
    float4 FocusTexel;
};

// Metres from the camera along its axis, from a [0, 1] depth.
float focusLinearDepth(float deviceDepth)
{
    const float near = FocusLens.x;
    const float far = FocusLens.y;
    return (near * far) / max(far - deviceDepth * (far - near), 1e-6f);
}

// The circle of confusion as a signed fraction of the widest: negative in front
// of the sharp band, positive behind it, zero inside. **The sky is infinitely
// far**, so it takes the far side's whole softness rather than the far plane's.
float focusCircle(float deviceDepth)
{
    const float farEdge = FocusBand.x + FocusBand.y;
    const float nearEdge = max(FocusBand.x - FocusBand.y, 0.0f);
    if (deviceDepth >= 1.0f)
        return FocusBand.w;
    const float metres = focusLinearDepth(deviceDepth);
    if (metres > farEdge)
        return FocusBand.w * saturate((metres - farEdge) / max(farEdge, 1.0f));
    if (metres < nearEdge)
        return -FocusBand.z * saturate((nearEdge - metres) / max(nearEdge, 0.5f));
    return 0.0f;
}
#endif

#if defined(LUAUG_UNIFORMS_RAYS)
// `render::GpuLookRaysUniforms`, 32 bytes.
cbuffer GpuLookRaysUniforms : register(b0, space3)
{
    // xy where the sun is on the screen, in texture space -- it may be off the
    // screen; z how present it is, 0 to 1, with the intensity folded in; w the
    // screen's width over its height.
    float4 RaysSun;
    // x how far towards the sun the gather reaches, as a fraction of the way;
    // y how many taps; z how much each tap fades from the last; w unused.
    float4 RaysGather;
};
#endif

#if defined(LUAUG_UNIFORMS_AIR)
// `render::GpuLookAirUniforms`, 128 bytes.
cbuffer GpuLookAirUniforms : register(b0, space3)
{
    column_major float4x4 AirInverseViewProjection;
    // x how much the air hides per metre at the height of `Offset`, y how fast
    // that falls per metre of height, z the camera's height above `Offset`,
    // w how much thicker it grows towards the horizon.
    float4 AirDensity;
    // rgb the air's own light, w how far a ray into the sky is taken to go.
    float4 AirLight;
    // rgb the glare's light towards the sun, already scaled by `Glare`; w the
    // share of the air a ray into the open sky counts.
    float4 AirGlare;
    // xyz towards the sun, w how tight its glare lobe is.
    float4 AirSun;
};

// How much air a ray crosses from the camera, `reach` metres along a direction
// whose vertical part is `rise`: the integral of an exponential in height along
// a straight line, which has a closed form. `render::airOpticalDepth` is the
// same function on the CPU, and a test holds the two to each other's numbers.
float airOpticalDepth(float rise, float reach)
{
    const float atCamera = AirDensity.x * exp(clamp(-AirDensity.y * AirDensity.z, -60.0f, 60.0f));
    const float k = clamp(AirDensity.y * rise * reach, -60.0f, 60.0f);
    // (1 - e^-k) / k, which tends to 1 as the ray runs level or the air stops
    // thinning -- written as its series there, where the quotient is 0 / 0.
    const float along = abs(k) > 1e-4f ? (1.0f - exp(-k)) / k : 1.0f - 0.5f * k;
    const float horizon = 1.0f - abs(rise);
    const float haze = AirDensity.w * AirDensity.x * pow(horizon * horizon, 4.0f);
    return atCamera * reach * along + haze * reach;
}
#endif

#endif // LUAUG_LOOK_HLSLI
