# 0037 — The RHI interface is frozen at the end of M4

- Status: accepted
- Date: 2026-08-20

## Context
ADR 0005 built a custom ~40-call RHI so the graphics backend is replaceable, with
SDL3 GPU as the desktop default and bgfx as the mobile and low-end hedge. An
abstraction is only honest if a second implementation can satisfy it, and the
roadmap therefore set a deadline: **end of M4 = RHI interface freeze**, because
by then a real renderer has exercised it and before then it has not.

M4 is what exercised it. The pass list built on this seam — a depth-only shadow
pass into a sampled `D32Float`, a fullscreen sky, a forward PBR pass into an
`Rgba16Float` target with per-stage uniforms and five sampled textures, and a
tonemap resolve to the swapchain — is the first thing to ask the interface for
more than a clear and a line list. Every call it needed already existed. Nothing
was added to the seam for it, and the two additions the renderer wanted were
declined:

- an "upload without a command list", wanted for one-pixel default textures at
  `create` time, was replaced by uploading them on the first frame — a call
  added on the eve of a freeze for a caller that has another way is the shape of
  the mistake this ADR exists to prevent;
- nothing else.

## Decision
The surface below is **frozen**. Adding, removing or changing the signature of
any of it requires a human-approved ADR, exactly as changing a pinned dependency
does (R5).

`rhi::IDevice` — `backend`, `caps`, `claimWindow`, `releaseWindow`,
`createBuffer`, `createTexture`, `createSampler`, `createShader`,
`createGraphicsPipeline`, `destroy` (per handle type), `beginFrame`,
`acquireSwapchain`, `submitAndPresent`, `waitIdle`, `readTexture`.

`rhi::ICmdList` — `beginRenderPass`, `endRenderPass`, `setPipeline`,
`setViewport`, `setScissor`, `bindVertexBuffers`, `bindIndexBuffer`,
`bindUniforms`, `bindTextures`, `draw`, `drawIndexed`, `upload`, `uploadTexture`,
`pushDebugGroup`, `popDebugGroup`.

Thirty-two calls, against the "~40" ADR 0005 budgeted.

Also frozen: the descriptor structs in `rhi/descs.h` and the enumerations in
`rhi/types.h` that those calls take, since a signature is only as fixed as the
types in it.

**What is NOT frozen**, and deliberately: `rhi::Capabilities`. ADR 0023 already
says optional-feature queries grow as backends arrive, and a capability is an
answer about a device rather than a call a caller makes — widening it cannot
break an existing implementation.

Compute is absent from the frozen set because it is absent from the interface:
ADR 0005 lists Compute among the concepts the RHI covers, and no v1 render path
needs a dispatch. Adding it is an ADR, and the milestone that wants it (a
GPU-driven path gated on `Capabilities`) is the one that should write it.

## Consequences
`rhi_capture` and `rhi_null` are the proof the surface stayed neutral: both
implement all thirty-two calls with no SDL type in sight, and the capture
backend records every one of them deterministically — a call that could not be
recorded that way would have been shaped wrong, and none was.

The cost is that a backend need the renderer discovers later is now a
conversation rather than an edit. That is the intent: bgfx has to satisfy this
interface without it having been shaped around SDL_GPU's convenience, and an
interface that keeps growing to fit its first implementation is not an interface.

**This ADR is written before the Android device checkpoint the roadmap requires
has happened.** That checkpoint asks a question this freeze cannot answer — ADR
0005 records SDL3 GPU's Android support as officially "limited", with bgfx as the
hedge — and if a device says the default backend does not work there, the answer
is a second backend behind this same interface rather than a different interface.
The freeze is what makes that answer possible; it is not a claim that the
question has been settled. The checkpoint's result is recorded in
`docs/briefs/m4-kickoff.md`.
