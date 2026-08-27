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

It carries the whole pass list -- shadow map, sky, forward PBR, the blended pass
and tonemap -- and the image agrees with the Tier-1 one on everything that
matters: lit ground, cast shadows, the four materials reading as the metals and
dielectrics they are, and the pane fading rather than switching.

**Re-recorded at M4.5**, with the rest of M4's artifacts, for the reason that
milestone exists: the first one was rendered by a renderer that never read
`Lighting`, so it showed a scene lit by a sun standing straight up rather than
by the one its script describes.

**Nothing compared it, deliberately** -- and that has been resolved rather than
left open. A software rasterizer's output is not bit-identical to a discrete
GPU's, and the pixel gate's own note above explains what happens to a golden
that tries to span both. What the note did not say is that a golden does not
have to span them: **lavapipe is bit-identical to ITSELF**, measured at zero
differing pixels across two runs at tolerance zero, so a lavapipe golden
compared only on lavapipe is an exact instrument rather than a fuzzy one.

`lavapipe/` below is that comparison, and this file stays as the M4 and M4.5
evidence the two briefs cite.

Getting it at all needed `mesa-vulkan-drivers` in the Tier-2 image. The image
already carried `libvulkan1`, which is only the loader: with no ICD installed,
`SDL_CreateGPUDevice` reported "No supported SDL_GPU backend found" and every
pixel test on that tier skipped. Installing the driver turned those skips into
failures, which is how it emerged that `-LE gpu-golden` -- written into
`engine/app/CMakeLists.txt` as the thing that must happen to these tests -- had
never actually been applied on the Linux tier. The absence of a device had been
doing that job by accident.

## `daystrip/` — the M4.5 deliverable, as one image

A copy of `examples/02-meshes`'s scene with the orbit taken out, so the camera
holds still and `ClockTime` is the only thing that changes. `scripts/daystrip.ps1`
renders it once per hour of the day and lays the frames side by side with
`imgstrip`, producing `docs/images/daystrip.png`.

The strip exists because the milestone's claim is about change over time -- the
sun crossing, shadows lengthening, `Transparency` passing through every value --
and no single screenshot can carry that. A fixed camera is what makes it
readable: with an orbit in it, nobody can say which variable moved the shadow.

`ClockTime` there is a function of the tick count, so `--frames=N` selects the
hour and the host's exit screenshot is that hour's frame. That is why one frame
needs one run and why no new flag was added to get it.

## `lavapipe/` — the nightly real-image suite (S7.6)

Both `docs/architecture.md` §9 and `docs/roadmap.md` promise this by name: "a
small real-image golden suite (lavapipe on Linux, WARP/D3D12 on Windows) runs
nightly, non-blocking". Neither had it until now — what existed was the single
recorded PNG above.

Two scenes, compared **exactly**: tolerance 0, zero pixels allowed to differ.

| Golden | Scene | Frames | Why this one |
|---|---|---|---|
| `meshes.png` | `examples/02-meshes` | 30 | The whole pass list at once — shadow map, sky, forward PBR, the blended pass, tonemap — and four materials that have to read as the metals and dielectrics they are |
| `specular.png` | `tests/screenshots/specular` | 120 | The INSTANCED path, and it is here for D043: that path shipped drawing **nothing at all** while every gate it had was satisfied. The draw count fell from 15,250 to 22, the command stream matched its capture, and the frame got eight times faster — because the geometry was being transformed off-screen. A command stream and a counter can both be right about a frame that is empty |

**Exact, and that is the point.** The tolerance-2 gate above exists because two
GPUs round the last bit of a unorm conversion differently. Comparing a software
rasterizer against itself has nothing for a tolerance to absorb except a real
change, so there is none — and a single moved pixel is a finding rather than
noise. Break-verified by rendering the same scene one frame short, which differs
in 196,196 pixels.

**Non-blocking, and it has to be.** A Mesa upgrade moves every pixel of both,
and that is not a defect in this repository. Nothing registers these tests
unless asked: `LUAUG_LAVAPIPE_GOLDENS` is OFF, so a normal `ctest` never sees
them and a normal gate cannot go red on them.

```
scripts/localgate.ps1 -Only lavapipe            # compare
scripts/localgate.ps1 -Only lavapipe -Record    # re-record, after a Mesa move
```

The nightly job runs the same `scripts/gates/lavapipe-goldens.sh` and uploads
the images it produced whether or not they matched — a mismatch is exactly the
case somebody needs the pictures for, and the driver script writes a
`.diff.png` beside each output showing which pixels moved.

Recorded on `llvmpipe (LLVM 20.1.2, 256 bits)`, Mesa 25.2.8, in the Tier-2
container. The gate prints the driver it found before it compares, so a run
against a different one says so rather than producing a wall of differing pixels
with no explanation.
