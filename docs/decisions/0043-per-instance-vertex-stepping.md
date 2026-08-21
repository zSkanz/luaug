# 0043 — The frozen RHI gains per-instance vertex stepping, and nothing else

- Status: proposed — awaiting human approval (ADR 0037 requires it)
- Date: 2026-08-21

## Context

ADR 0037 froze the RHI at thirty-two calls plus the descriptor structs those
calls take, and said that adding to it "requires a human-approved ADR, exactly as
changing a pinned dependency does (R5)". ADR 0038 predicted M7.5 would be the
milestone that tested the freeze: *"Cubemaps, mip-chain generation, compute for
prefiltering, and array textures for cascades are the most likely places the
frozen 32 calls turn out to be 32 minus something."*

It was tested, and three of those four predictions were wrong in the useful
direction — the technique changed instead of the interface:

- **Array textures for cascades** were not needed: four cascades live in one 2×2
  atlas, addressed by `setViewport`, which the frozen interface already has.
- **Cubemaps and mip-chain generation** were not needed: the prefiltered
  environment is an octahedral 2D texture whose levels are prefiltered on the CPU
  and uploaded through `uploadTexture(texture, data, mipLevel)`, which already
  takes a mip level.
- **Compute for prefiltering** was not needed for the same reason.
- A **hardware comparison sampler**, which ADR 0038 names in its scope, was not
  needed either: `Gather` returns the four texels `SampleCmp` would have
  bilinearly weighted, and the four comparisons and two lerps are six ALU
  instructions. `SamplerDesc` gains no `compareEnable`.

One thing was needed, and it is the one the frozen struct's own comment already
anticipated by name.

`rhi::VertexBufferLayout` reads:

```cpp
struct VertexBufferLayout
{
    u32 slot = 0;
    u32 strideBytes = 0;
    // Per-instance stepping arrives with instanced rendering (M4); leaving it
    // out keeps the capture stream from carrying a field no backend reads.
};
```

M7.5's scope includes instanced draws, measured rather than assumed: two thousand
enemies sharing one mesh cost 11.1 ms a frame, of which 6.9 ms is submitting
2,092 forward draws, and the same scene costs the same at 320×180 and at 4K —
the GPU is idle and the frame is CPU-side submission
(`docs/perf-baselines.md`, M2 table). Removing that means one draw call for a run
of objects that share a mesh and a material, with each instance's transform read
from a second vertex stream that advances per instance rather than per vertex.

## Decision

**`rhi::VertexBufferLayout` gains one field:**

```cpp
    // When true, this stream advances once per INSTANCE rather than once per
    // vertex. False is the default and is what every stream before instanced
    // rendering existed was.
    bool perInstance = false;
```

That is the entire change to the frozen surface. No call is added, no call
changes signature, and no other struct or enumeration is touched.

Every backend implements it:

- `rhi_sdlgpu` maps it to `SDL_GPU_VERTEXINPUTRATE_INSTANCE`, leaving
  `instance_step_rate` at zero -- SDL_gpu.h:1651 calls that field "Reserved for
  future use. Must be set to 0" and asserts on anything else. The rate is implied
  by the input rate, and one step per instance is the only one SDL_GPU offers.
  Which is also why the RHI field is a `bool` and not a count: an interface that
  offered a step rate would be offering something no backend here can honour.
- `rhi_capture` records the count of per-instance streams on the
  `createGraphicsPipeline` line, so a golden can see that a pipeline is instanced
  — a capture backend that could not see the change would make the render
  regression gate blind to precisely the thing this milestone adds.
- `rhi_null` needs no change, which is what a null backend is for.

## Alternatives considered

- **Per-instance data in a uniform block, indexed by `SV_InstanceID`.** Needs
  nothing new, and was the near miss. It fails on a number: SDL_GPU's Vulkan
  backend caps one uniform push at `MAX_UBO_SECTION_SIZE`, which is 4,096 bytes
  (`third_party/sdl3/src/gpu/vulkan/SDL_gpu_vulkan.c:71`) — sixty-four 64-byte
  transforms per push. Two thousand enemies would be thirty-two draws instead of
  one, in three passes, with 125 KB of uniform-ring churn per frame per pass. It
  is a 62× improvement rather than a 2,000× one, and it spends the same bytes
  through a path that is not built to stream them.
- **Per-instance data in a texture, indexed by `SV_InstanceID`.** Also needs
  nothing new, and fails on a different number: `uploadTexture` writes a whole
  mip level, so a texture sized for the worst case uploads its whole self every
  frame — four megabytes to draw two hundred enemies — while
  `upload(buffer, data, offset)` writes exactly the prefix in use.
- **Leave instancing out of v1.** Rejected by the roadmap, which makes the
  instanced path a gate item with a number attached: *"the count of draw calls
  stated beside the count of visible objects — if those two numbers are still
  equal, the instanced path is not doing anything."*

## Consequences

- **The freeze held for everything else in the milestone that most stressed it.**
  Four techniques that would each conventionally want an interface change were
  built without one, and the one field that landed is the one a comment written
  before the freeze had already reserved. That is the freeze working as ADR 0037
  intended: a backend need is a conversation rather than an edit, and the
  conversation mostly ends in "use the interface differently".
- **The bill for the workarounds is real and is recorded** so nobody reads this
  as free. The environment prefilter runs on the CPU and is amortised one mip per
  frame; the cascade atlas costs a per-tile clamp on every shadow tap; the
  cluster light lists travel as `Load`-addressed textures because there are no
  storage buffers. Each is defensible on its own, and together they are the price
  of thirty-two calls staying thirty-two.
- **bgfx must satisfy this too**, and does trivially: per-instance stepping is a
  vertex-layout property in every graphics API this interface abstracts.
- **A rejection is contained.** If the human declines, the uniform-block
  alternative above is the fallback and it is a change to the renderer only —
  the shader reads a transform from a different place and the pass list splits
  its runs at sixty-four. Nothing outside `render` would move.
