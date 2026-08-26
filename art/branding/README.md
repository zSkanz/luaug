# art/branding/ — the request for a new LuauG mark

`branding/` at the repository root is what ships. **This is where a replacement
is drawn and argued about**, the same split `art/editor-icons/` has against
`icons/`.

**Nothing here is adopted by being delivered.** Public-facing branding is a human
decision (`MASTER_PROMPT.md` §10). What this directory produces is a sheet of
candidates and an honest reading of them, and only then does anything move into
`branding/`.

## Decided, 2026-08-23

**The mark is `C1`** — the crescent with an orbit ring and a satellite — being
refined for ring weight. **The wordmark is `LuauG` set in Inter Bold 700 at −1.5%
tracking**, one ink and one weight.

**Both decisions were delegated by our human to the reviewer**, explicitly and in
those terms, after seeing the sheet of eight. Recorded here because §10 says this
choice is his: it was, and he spent it by handing it over. Anyone reading this
file later should know the delegation happened rather than assume the process was
skipped.

The reasoning is in `art/communication/for-cowork/046`. The short form: `C1` is
the only candidate that is *a thing* rather than a control somebody has already
built — the others are a loading spinner, a Rubik's cube, the Microsoft logo, an
eye, and the fullscreen button — and it is the only one whose one-colour version
loses nothing. The `G` is not coloured separately, because splitting `Luau` from
`G` points at the language, and R7 and ADR 0020 are the whole reason this engine
does not do that.

## Why we are replacing what is there

Two reasons, and the second is the load-bearing one.

**The mark is `</>`.** That is the most-used symbol in developer tooling —
editors, playgrounds, code-sharing sites, a hundred npm packages. It says *code*,
and specifically it says *web*. This is a 3D game engine with a fixed-tick
simulation and a physics backend, and at 16 px in a taskbar the `</>` is
indistinguishable from four other things already on that taskbar. A mark's whole
job is to be the one you find without reading.

**The artwork is a traced bitmap, not a drawing.** `branding/README.md` says so
plainly: 31 paths, around thirty near-identical colours, coordinates carrying
eleven decimal places, and the counters of the letters existing as separate green
shapes because in a bitmap the inside of an `a` *is* background. That is why the
letterforms are irregular — the `a` and the two `u`s do not match each other, and
the `G` carries a notch nobody designed. You cannot fix that by editing it. It
has to be redrawn.

## The one rule that decides the whole approach

**The model draws the MARK. It does not draw letters.**

An image model cannot letter. Asking one for "LuauG" is how the current
letterforms happened, and a second attempt would produce a second set of wrong
letters at a different angle. The wordmark instead gets **set in Inter** —
already vendored at `third_party/inter`, OFL-1.1, and already the engine's
default typeface by human decision (2026-08-20, roadmap M7). The UI and the
wordmark then share a face, which is a coherence the current pair does not have.

So the deliverable from the drawing agent is **one square symbol**, and
everything else is derived here.

## What we need, once a mark is chosen

| | What | Where it comes from |
|---|---|---|
| **mark** | the symbol alone, square | drawn |
| **wordmark** | `LuauG` set in Inter, transparent | set here |
| **lockup, horizontal** | mark + wordmark side by side | composed here |
| **lockup, stacked** | mark over wordmark | composed here |
| **one-colour** | the mark flattened to a single fill | derived — see below |
| `icon/luaug-{16..256}.png` + `.ico` | what an OS asks for | rasterised here |
| repo social card, 1280×640 | GitHub's link preview | composed here |

**The SVG is hand-authored from the drawing, not traced.** Tracing is what
produced the mess we are replacing. This only works if the mark is built from
geometry a person can measure and rewrite — circles, regular polygons, constant
strokes, shared centres. That is a constraint on the *design*, not on the
delivery, and it is stated in the brief below.

## Derived, and one decision made while deriving

`art/branding/derived/` holds the full set built from `C1-r20` and the vendored
Inter: the seven icon rasters, the `.ico`, both lockups, the one-colour mark, the
512 px wordmark and the 1280×640 repository card. It is staged rather than
written over `branding/` on purpose — choosing a drawing and replacing assets
that already ship are two different acts.

**The mark is one colour, and the navy satellite is gone.** On a dark taskbar
navy `#0B2A45` disappears into the background, so a two-colour mark needed two
files for something Windows cannot switch between. It never needed the colour:
the *gap* around the satellite already makes it a distinct object, which is the
rule that killed candidate `A1` in the first place. One file now serves both
panels.

**The `.ico` is hand-built, and every entry is PNG.** Pillow writes BMP entries
below 256 px; `engine/platform/tests/platform_tests.cpp` asserts the first four
bytes of the icon the executable carries are a PNG signature, because the window
icon is decoded by `stb_image` and nothing in this engine reads a DIB. The
structure is written by hand for that reason. If it is ever regenerated with a
tool, that test is where it will say so.

**The wordmark's record is its recipe, not an SVG.** Inter Bold 700, optical size
32, tracking −1.5%, from `third_party/inter`. The recipe reproduces it exactly and
names the typeface; a file of outline curves does neither. This is the same trade
`branding/README.md` already makes for the rasters and ADR 0032 makes for DXC —
commit the artifact, write down where it came from.

## Reference images — when a picture goes in with the prompt

The model accepts a **reference image** alongside the text (confirmed by our
human, 2026-08-23). This is the tool that makes iteration possible at all:
describing a drawing in words produces a *different* drawing, never a *closer*
one, and four of the candidates below cost a generation each to learn that.

- **First draft of an idea → words only.** A reference anchors, and the point of
  a first draft is that nothing anchors it.
- **Refining a candidate → attach the candidate**, name what changes, and name
  what must not. "This image, ring one and a half times thicker, everything else
  identical."
- **Never as an outside style reference.** Not another company's logo, not "in
  the style of". Rule 7 below is a legal line, not a preference.

## The rules a candidate has to satisfy

1. **It reads at 16 px.** Not "is still visible" — is still *identifiable*. Every
   candidate is judged on a sheet at 16 / 24 / 32 / 256, both panels, before
   anything else is discussed. This is the same rule that settled every argument
   in the icon set, and it settled them by being looked at rather than measured.
2. **It survives one colour.** Flatten every fill to a single black and it must
   still be the mark. A logo that needs its second colour to be legible has no
   favicon, no engraving, no monochrome print, no dark-panel version.
3. **Flat vector.** Solid fills, no gradient, no bevel, no drop shadow, no
   3D render, no glow, no texture, no perspective mock-up.
4. **Geometric enough to be rewritten by hand.** See above.
5. **At most two colours plus white.**
6. **No letters.** No `L`, no `G`, no monogram — see the rule above. A shape that
   happens to suggest a letter is fine; a shape that IS a letter is not.
7. **It resembles no existing engine's mark.** Not Unity, not Unreal, not Godot,
   not Bevy, not O3DE, not Stride — and not Roblox, which is a hard legal line
   here and not a taste one (R7, ADR 0020). Also not Lua's own planet-and-moon:
   Luau descends from Lua and a nod would be honest, but a near-copy of another
   project's logo is not a nod.
8. **Square, generous margin, centred.** The mark is cropped to its own bounds
   later; what matters is that nothing touches the edge.

## The four directions

Drawn as candidates, not as a shortlist to narrow. Our human sees all of them.

**A — the tick.** A ring built of discrete equal segments with a single one
filled, reading as one step of a fixed timestep. This is the engine's actual
distinguishing claim — deterministic simulation on a fixed tick — and no other
engine's mark says it. Strong at 16 px because it is a ring, which is the most
robust small shape there is.

**B — the chunk.** A cube built from smaller cells, one corner cell detached and
floating slightly away, for the streaming world and the ECS underneath it. Reads
as *3D* immediately, which `</>` never does. The risk is Minecraft, and the
defence is that the cells are visible as a lattice rather than as blocks.

**C — the crescent.** `lua` is *moon*, and Luau is what this engine embeds. A
crescent with a small satellite disc on a defined orbit. Honest lineage, warmest
of the four, and by far the most distinctive silhouette at 16 px. Must not
resemble Lua's own planet-and-orbit mark — different construction, and the
crescent rather than the full disc is most of that difference.

**D — the frame.** Four corner brackets around empty space, as a viewport or a
selection. It is what the editor draws around the thing you have clicked, so the
mark would be the engine's own gesture. Simplest to make read at 16 px, and the
weakest concept of the four — corner brackets are common in camera and photo
tooling.

Each is drawn **twice**: once as specified, once with the drawer's own reading of
the same idea. Eight candidates, one sheet.
