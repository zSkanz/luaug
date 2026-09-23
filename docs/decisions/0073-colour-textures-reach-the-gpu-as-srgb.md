# 0073 — Colour textures reach the GPU as sRGB, compiled or loose

- Status: accepted
- Date: 2026-09-23
- Milestone: V1 follow-up (block images), found on the way
- Decided by: the agent, under the owner's standing instruction of 2026-08-26 to
  take the repository's decisions on their behalf.

## Context

Giving block faces images showed them pale and washed out. The cause was not in
the block code, and it was two defects that had been hiding each other:

- **The compiled path ignored the transfer function.** The asset compiler
  encodes a base colour, an emission and any image no material uses as data in
  sRGB, and records that in the texture (`TextureAsset::srgb`). The renderer's
  upload mapped every BC format to its linear name regardless -- because the RHI
  had no sRGB BC formats to map to -- so every compiled colour was sampled as if
  it were already linear. E9 had fixed the encoding and the fix never reached
  the screen.
- **The loose path uploaded every image as linear**, whatever it was for, so a
  project's colours changed the moment it was compiled.

## Decision

1. **The RHI gains `Bc1RgbaUnormSrgb`, `Bc3RgbaUnormSrgb` and
   `Bc7RgbaUnormSrgb`**, after the freeze of ADR 0037. It is additive -- three
   enumerators and their backend names -- and it is the only way a compressed
   colour can be decoded from sRGB by the sampler, which is where it has to be
   for filtering and mip averaging to happen in linear light.
2. **The compiled upload honours `srgb`**: a colour gets the sRGB format, a
   normal map (BC5) never does.
3. **The loose upload knows what the image is for**: base colours, emissions and
   block images are sRGB; normal and metal/roughness maps stay linear. A URN
   used both ways takes whichever asked first, which is a project mistake the
   compiler would also have to guess about.
4. **UI images are untouched.** The UI draws after tone mapping, straight into
   the display target, where a PNG's values already are what the screen should
   show.

## Consequences

- Every compiled model's colours become what their authors painted: darker and
  more saturated than this engine showed them before. Nothing a gate pinned
  moved -- the goldens use generated or loose-linear inputs -- so this is a
  visible change to projects, not to tests, and it is the correct one.
- A block image, a material map and a model's texture now look the same before
  and after `luaug build`.
