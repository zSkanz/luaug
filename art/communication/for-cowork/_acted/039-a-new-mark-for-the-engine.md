# 039 — new work: the engine's own mark. Eight candidates, one sheet.

From the reviewer. Different job from the icon set and a different prompt block —
do not reuse Part A, that block is for flat white masks and this needs colour.

Brief: **`art/branding/README.md`**. Read it, because it carries the reasoning
and this message only carries the prompts. Deliver into
`art/branding/candidates/`.

## What is being replaced and why

The mark today is **`</>`**. That is the most-used symbol in developer tooling
and it says *web*, not *3D game engine* — at 16 px on a taskbar it is four other
things. And the artwork is a **traced bitmap**: 31 paths, ~30 near-identical
colours, eleven decimal places per coordinate, letter counters existing as
separate green shapes. The irregular `a`, the mismatched `u`s and the notch in
the `G` are all that. It cannot be edited into shape.

## The rule that shapes your whole job

**You draw the MARK. You do not draw letters.**

An image model cannot letter, and asking one to is exactly how the current
letterforms happened. The wordmark gets **set in Inter** — already vendored,
OFL-1.1, already the engine's default typeface by human decision. I compose it
here. So there is no `LuauG`, no `L`, no `G` and no monogram in anything you
send. A shape that happens to suggest a letter is fine; a shape that is one is
not.

Second rule that constrains the drawing rather than the delivery: **the SVG will
be hand-authored from your PNG, not traced.** Tracing is what we are undoing. So
the mark has to be geometry a person can measure and rewrite — circles, regular
polygons, constant stroke widths, shared centres, angles that are multiples of
something. An organic blob is a beautiful thing that we cannot ship.

## Part L — the style block. Constant, prepend to every one of the eight.

```
A flat vector logo mark for a professional software product, drawn as a single
centred symbol on a pure white background. Solid flat fills only: no gradient,
no bevel, no drop shadow, no glow, no texture, no 3D render, no perspective, no
mock-up, no reflection, no outline glow. At most two colours plus white.
Geometric construction: true circles, regular polygons, constant stroke widths,
shared centres, angles at clean multiples. Generous even margin, nothing
touching the edge, square composition. No text, no letters, no numbers, no
wordmark, no monogram. It must remain identifiable when reduced to 16 pixels and
when flattened to a single solid colour. Primary colour a bright azure blue
(#12B0FF); the optional second colour is a deep navy (#0B2A45).
```

Then a blank line, then the subject line.

## The eight subjects

Four directions, each drawn twice — once as written, once as your own reading of
the same idea. **Your second version is not a safety copy; it is the one where
you disagree with me.** That has been worth more than my specifications four
times today.

Filenames: `A1.png` `A2.png` … `D1.png` `D2.png`.

**A — the tick** (deterministic fixed timestep, the engine's real claim)

> A1: a ring made of twelve equal separated segments, all in the azure blue
> except one which is navy, the filled one at the top. Even gaps, constant
> segment width, true circle.

> A2: your own reading of "one step of a fixed timestep, as a ring".

**B — the chunk** (streaming world, ECS)

> B1: a cube seen in isometric projection, drawn as a lattice of smaller cells
> rather than solid, with one corner cell detached and floating a short distance
> away from it. Constant line weight, true isometric angles.

> B2: your own reading of "a world made of chunks, one of them separate".

Watch for Minecraft on this one. The defence is that it reads as a *lattice*, not
as stacked blocks.

**C — the crescent** (`lua` is *moon*; Luau is what this engine embeds)

> C1: a bold crescent moon, thick and geometric rather than thin, with a small
> solid disc sitting off its open side on a defined circular orbit. Both shapes
> built from true circles.

> C2: your own reading of "a moon and something in orbit around it".

**Must not resemble Lua's own planet-and-orbit mark.** The lineage is honest;
copying the logo is not. The crescent instead of a full disc is most of the
difference — hold on to that.

**D — the frame** (the viewport, and what the editor draws around a selection)

> D1: four thick right-angled corner brackets arranged as the corners of a
> square, with empty space between them, all four identical and equally inset.

> D2: your own reading of "the corners of a viewport".

Weakest of the four and I know it — corner brackets are all over camera and photo
tooling. Drawn anyway because it is the engine's own gesture and I would rather
our human reject it having seen it.

## What I will do when they land

Sheet all eight at **16 / 24 / 32 / 256, both panels, and again flattened to one
colour**, and put it in front of our human with an honest reading. Two rules
decide it before taste gets a turn:

- **Identifiable at 16**, not merely visible.
- **Survives one colour.** A mark that needs its second colour to be legible has
  no favicon and no monochrome anything.

**Nothing is adopted by being delivered.** Branding is a human decision here
(`MASTER_PROMPT.md` §10) and this one is not mine or yours.

## One thing to avoid that is a legal line and not a taste one

It must resemble no existing engine's mark — Unity, Unreal, Godot, Bevy, O3DE,
Stride — and **not Roblox**, which for this repository is R7 and ADR 0020 rather
than an aesthetic preference. If a draft starts drifting toward any of them,
change the construction rather than nudging the proportions.
