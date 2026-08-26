# branding/ — the LuauG logo and icon

Public-facing branding is a human decision (`MASTER_PROMPT.md` §10), which is
why these live at the top level rather than buried under `docs/`.

| File | What it is |
|---|---|
| `luaug-mark.svg` | **The source of truth.** The mark, written as four circles and one arc. |
| `icon/luaug-{16..256}.png` | The mark at each size an OS asks for, rendered from the SVG's numbers. |
| `icon/luaug.ico` | The same set as one Windows icon resource. Every entry PNG — see below. |
| `luaug-mark-512.png` | Raster mark for anywhere a PNG is wanted. |
| `luaug-logo-512.png` | The wordmark, set in Inter. See *the wordmark has no SVG*. |
| `luaug-lockup-horizontal.png` | Mark beside wordmark. The default lockup. |
| `luaug-lockup-stacked.png` | Mark over wordmark, for square-ish space. |
| `luaug-social-card.png` | 1280×640, GitHub's link preview. |

## The mark

A crescent inside an orbit, with a satellite sitting on the ring where the ring
breaks for it. `lua` is *moon*, and Luau is what this engine embeds — the lineage
is honest, and the mark is not Lua's own planet-and-orbit.

**It is one colour.** The design had a navy satellite; navy disappears on a dark
taskbar, and a two-colour mark would then need two files for something no OS
switches between. It never needed the colour, because the **gap** around the
satellite already makes it a distinct object. That rule — *a distinction carried
by colour is not a distinction* — is what eliminated three of the eight
candidates this replaced, and it applies to the finished mark too.

**Recolour it freely.** One fill, one stroke, both `#12B0FF`. There is no dark
variant file and there must not be one: two drawings of the same mark drift.

## Why the icon is the mark and not the wordmark

The wordmark is about 3:1. Rendered into the square an OS icon actually gets it
is a smear at 16 px. The mark reads at every size down to 16, which was tested
before it was chosen rather than assumed after.

## `luaug-mark.svg` is written, not traced

Four circles and one circular arc, whole numbers, 1.7 KB.

What it replaced was an **autotraced bitmap**: 31 paths, around thirty
near-identical colours, coordinates carrying eleven decimal places, and the
counters of the letters existing as separate green shapes because in a bitmap the
inside of an `a` *is* background. That is why the old letterforms were irregular
and why they could not be corrected — there was nothing to correct, only pixels
that had been guessed at.

The rasters in `icon/` are rendered from this file's numbers, restated once in
the build script. **There is no SVG rasteriser in this repository and there must
not be one**: adding a dependency for six icons would be an ADR for an artifact
that changes when the logo changes, which is approximately never. Same trade
ADR 0032 makes for DXC — commit the artifact, write down where it came from.

## The wordmark has no SVG, and that is the better record

`LuauG` is **set in Inter Bold 700, optical size 32, tracking −1.5%**, from
`third_party/inter` (OFL-1.1) — already the engine's default typeface by human
decision, so the UI and the wordmark share a face.

The record for it is that recipe, not a file of outline curves. The recipe
reproduces it exactly *and* names the typeface; Béziers do neither.

**The `G` is not coloured or weighted separately.** Beyond the fact that such a
device dies when the wordmark is flattened to one ink, splitting `Luau` from `G`
points at the *language* — and R7 and ADR 0020 are the whole reason this engine
does not present itself as Roblox-adjacent. It is one name.

**Never ask an image model for the letters.** That is what produced the wordmark
this replaced, whose two `u`s did not match each other.

## Every entry of `luaug.ico` is PNG, and it is written by hand

The window icon is decoded by `stb_image`, and a BMP-encoded icon entry would
need a DIB reader nothing in this engine has. Pillow writes BMP entries below
256 px, so the ICONDIR structure is assembled by hand.

`engine/platform/tests/platform_tests.cpp` asserts that the first four bytes of
the icon the executable carries are a PNG signature. If this is ever regenerated
with a tool, that test is where it says so — rather than in a window that quietly
has no picture.

## Where these get used

`branding/luaug.rc` compiles `icon/luaug.ico` into `luaug-host` and into the
platform test binary. A game built with `luaug build` carries *its* icon, not
ours; ours is the fallback (`tools/cli/commands/build.luau`).

## How this one was chosen

Eight candidates across four directions, in `art/branding/`, each judged on two
rules before taste got a turn: **identifiable at 16 px**, and **survives being
flattened to one colour**. Five of the eight failed by resembling something that
already exists — a loading spinner, a Rubik's cube, the Microsoft logo, an eye,
and the fullscreen button.

The brief, the candidates and the full argument are in
[`art/branding/README.md`](../art/branding/README.md).
