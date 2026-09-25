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

#endif // LUAUG_LOOK_HLSLI
