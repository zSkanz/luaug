# Golden screenshots

Reference images the engine's rendering is compared against, and the driver
that does the comparing.

## What this is for

`MASTER_PROMPT.md` §8 states the observation rule: *any change with visible
output must be verified by a screenshot, not by "the code looks right."* This
directory is how that verification runs without a human in the loop.

The flow is one CTest entry, `screenshot_gate`:

1. `luaug-host --headless --frames=N --exit --screenshot=out.png` renders on a
   real GPU into an offscreen target and reads the pixels back.
2. [`tools/imgcmp`](../../tools/imgcmp) compares `out.png` against the golden
   here, with a per-channel tolerance of 2 and zero differing pixels allowed.

## Why the numbers are what they are

**Tolerance 2, zero differing pixels.** GPUs round the last bit of a unorm
conversion differently, and a gate that fires on that is a gate someone
switches off. A real rendering change is never one pixel, so the pixel count
stays at zero.

**A fixed frame count.** Headless runs drive a synthetic clock — exactly one
fixed tick per frame — so frame 30 always produces the same image on any
machine at any speed. That is the property the whole gate rests on, and it is
why the loop does not read the wall clock in headless mode.

## When the golden changes

Replacing a golden is a claim that the rendering *should* look different. Say
so in the commit message and say why. A golden updated without explanation is
indistinguishable from a regression that was papered over — which is the
failure mode image gates die of.

The driver writes a `.diff.png` next to the output on failure. Look at it
before deciding.

## No GPU

The gate skips, it does not fail. `luaug-host` exits 4 when no graphics device
could be created, and the driver turns that into a CTest skip. A build that
goes red because a runner has no driver teaches people to ignore red builds —
and the roadmap already allows the M1 render gate to run on the dev machine for
exactly this reason.

## Not the same thing as the capture gate

This compares **pixels**, and it needs a GPU. The blocking render-regression
gate is the `rhi_capture` command stream, which needs none — see
`docs/architecture.md` §9. Real-image comparison is the agent's own
verification tool and, from M4, a nightly non-blocking job.

## `meshes-lavapipe.png` — the Tier-2 attempt (M4, non-blocking)

The roadmap's M4 gate asks for "Tier-2 lavapipe image goldens attempted
(non-blocking)". This is the attempt, and it is recorded as evidence rather than
compared against: `examples/02-meshes` at 960x540, thirty frames, rendered in the
Tier-2 container on Mesa's software Vulkan device (`llvmpipe (LLVM 20.1.2, 256
bits)`).

It carries the whole pass list -- shadow map, sky, forward PBR, tonemap -- and
the image agrees with the Tier-1 one on everything that matters: lit ground,
cast shadows, the four materials reading as the metals and dielectrics they are.

**Nothing compares it, deliberately.** A software rasterizer's output is not
bit-identical to a discrete GPU's, and the pixel gate's own note above explains
what happens to a golden that tries to span both. Promoting this to a comparison
is what the roadmap means by "stable for two consecutive milestones", and that
is M6's decision, not this one's.

Getting it at all needed `mesa-vulkan-drivers` in the Tier-2 image. The image
already carried `libvulkan1`, which is only the loader: with no ICD installed,
`SDL_CreateGPUDevice` reported "No supported SDL_GPU backend found" and every
pixel test on that tier skipped. Installing the driver turned those skips into
failures, which is how it emerged that `-LE gpu-golden` -- written into
`engine/app/CMakeLists.txt` as the thing that must happen to these tests -- had
never actually been applied on the Linux tier. The absence of a device had been
doing that job by accident.
