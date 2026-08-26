# The generation prompt

Two parts. **Part A never changes** — paste it every single time, unedited, or
the set stops matching itself. **Part B is one line** from the table in
[`README.md`](README.md).

Generate **one icon per request**. Asking for a sheet of nine gives nine icons
that share a canvas and not a style, and they cannot be separated cleanly.

---

## Part A — the style block (constant)

> Draw a single flat user-interface icon.
>
> **Canvas:** 1024 x 1024, perfectly square.
>
> **Colours:** the icon is one solid pure white shape (#FFFFFF). The background
> is solid pure black (#000000), edge to edge, with nothing else on it. Use only
> those two colours — no greys except the natural anti-aliasing at the shape's
> edge, no gradients, no shadows, no glow, no highlights, no reflections, no
> texture.
>
> **Form:** a flat silhouette seen straight on. No realistic perspective, no 3D
> shading, no bevel, no depth. Think of a road sign or a mobile status-bar icon,
> not an illustration and not a logo. The one exception is the conventional flat
> cube symbol — a SOLID hexagon with three narrow gaps cut from its centre to
> three alternating corners — which is allowed wherever the subject asks for a
> cube, because it is a symbol rather than a drawing of a box. It is solid with
> cuts, never an outline: the fill rule below has no exceptions and this is not
> one.
>
> **The keyline grid.** Leave 85 pixels of empty margin on all four sides, so
> the icon lives inside a centred 854 x 854 square. Inside it, the icon's
> dominant shape must fill exactly one of these four keylines, edge to edge:
>
> - a square shape fills 768 x 768
> - a circular shape fills a circle 854 across
> - a tall shape fills 683 wide by 854 high
> - a wide shape fills 854 wide by 683 high
>
> Pick the one keyline the subject naturally is and fill it. Do not draw the
> keyline, and do not leave the shape smaller than its keyline "to be safe" — an
> icon that under-fills its keyline looks shrunken beside the others, and that
> mismatch is the single most visible flaw in a set.
>
> **Fill, not outline.** The icon is a SOLID white shape. Where it needs an
> inner detail — a window inside a frame, a lens in a camera, a line of text in
> a label — that detail is a hole CUT OUT of the solid, showing the black
> background through it. Never draw a thin outlined shape with a hollow inside.
> A solid silhouette survives being shrunk to 16 pixels; an outline turns to
> grey mush at that size.
>
> **Constants.** Every stroke and bar is 85 pixels thick — the same 85
> everywhere, in every icon. Every rounded corner has a radius of 85 pixels. Two
> separate parts of the icon are never closer than 85 pixels. Nothing tapering,
> no varying line weight.
>
> **Two exceptions, and both matter:**
>
> **A thin cut scored INSIDE one solid shape is about 35, not 85.** The three
> lines that turn a hexagon into a cube are such a cut. At 85 they eat the shape
> and the cube becomes three petals at 16 pixels. Between separate parts, 85.
> Inside a single part, 35.
>
> **An icon that is a LINE rather than a body is drawn at 120, not 85.** A
> circular arrow, an ease curve, the rays of a sun. At 85 those strokes are
> thinner than one pixel once shrunk to 16, so they antialias to grey along
> their whole length and sit visibly faded beside the solid icons. 120 makes a
> stroke weigh what a body weighs. A stroke that is one part of an otherwise
> solid icon stays at 85.
>
> **Complexity:** at most four distinct shapes. The icon has to stay readable
> shrunk to 16 pixels, so no small internal detail.
>
> **No text.** No letters, no numbers, no words, no labels, no captions, no
> watermark, no signature. This is important: the icon must contain no writing
> of any kind.
>
> **The subject:**

## Part B — the subject line

Append the one-line subject from the table in `README.md`, then send.

---

## Why white on black and not transparent

Image models are unreliable about real alpha: they will often return a solid
background, or a drawn checkerboard that *looks* like transparency and is just
grey squares. Pure white on pure black is a mask, and turning a mask into alpha
is one mechanical step that never guesses wrong.

If the tool does produce genuine transparency, that is fine too — keep it. The
conversion handles both.

---

## Handing the files back

Save into [`src/`](src/) with the **exact class name** as the filename:

```
art/editor-icons/src/Part.png
art/editor-icons/src/MeshPart.png
art/editor-icons/src/WeldConstraint.png
```

Case-sensitive, no spaces, no suffixes, no version numbers. The name is the
lookup key, so `part.png` or `Part_v2_final.png` means a hand-written
translation table later, and a hand-written translation table is a thing that
goes stale.

Do not resize, crop, or convert anything. Deliver whatever the model produced.
Resizing, trimming, alpha extraction and the per-DPI sizes are one batch step
and doing them by hand introduces exactly the inconsistencies this brief exists
to prevent.

## Before handing back, check four things

1. The filename is a class name from the table, spelled exactly.
2. The image is square.
3. There is **no text anywhere** in it — models add letters unprompted, and it
   is the single most common thing to reject.
4. The shape is one connected-looking silhouette, not an illustration that
   happens to be white.

Anything that fails one of these is regenerated, not fixed by hand.
