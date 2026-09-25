# Orbit G — LuauG visual identity

Created 2026-09-23 in response to the request to refresh the engine logos in
the same visual family as the Orbit editor icons. This replaces the previous
crescent artwork in `branding/`. Earlier candidates and derived files remain
historical references.

## Exact design prompt

> Refine the LuauG Orbit G identity for use as a desktop application icon.
> Keep a single bold geometric capital G with rounded endpoints and joins.
> Remove the detached satellite so small sizes have one clear focal point.
> On a 64-unit canvas use a circle centered at (32, 32), radius 20,
> stroke 9; start at -48 degrees and travel counterclockwise through 312
> degrees to (52, 32), then draw the horizontal bar inward to (34, 32).
> For the application icon place the mint G (#55E0C5) on a graphite
> (#101C24) rounded square from (1, 1) to (63, 63), corner radius 14.
> Leave the outer corners transparent. Use no gradients, shadows, texture,
> satellite, small ornaments or outlines. Inspect native 16, 24 and 32 pixel
> exports on both light and dark surfaces. Keep transparent standalone marks
> for editorial use and the editor header, sharing exactly the same G geometry.
> Preserve the exact name LuauG in Inter weight 650, optical size 32, tracking
> -2.5% em. Preserve the existing mint, deep teal, graphite and paper palette.
> Export transparent horizontal and stacked lockups, light/dark/white/mono
> marks, a social card and application PNGs at 16, 24, 32, 48, 64, 128, 256
> and 512 pixels. Export an editable application SVG and a Windows ICO with
> PNG payloads at all seven sizes from 16 through 256 pixels.

This is the exact design specification used for authored vector geometry and
typesetting. No image model was used. Typography comes from the existing
OFL-licensed Inter font; no external artwork or font dependency was introduced.

## Geometry

- Canvas: 64 by 64 units.
- Orbit: center `(32, 32)`, radius `20`, stroke `9`.
- Start angle: `-48` degrees; counterclockwise sweep of 312 degrees to
  `(52, 32)`. Horizontal bar ends at `(34, 32)`.
- Rounded caps and joins; no detached satellite.
- Application tile: `(1, 1)` to `(63, 63)`, corner radius `14`.
- Minimum application icon: 16 pixels. Transparent marks and tiled icons
  share the same symbol, with no scaling or rotation inside the tile.

Revision 2026-09-24 supersedes the original 7-unit stroke and satellite:
those details lost definition at small OS icon sizes.

## Palette

| Token | Value | Use |
|---|---|---|
| Primary | `#17BDA6` | General transparent symbol |
| Deep teal | `#07867C` | Symbol on light backgrounds |
| Orbit mint | `#55E0C5` | Symbol on dark backgrounds and OS tile |
| Ink | `#14252D` | Light-background wordmark and monochrome symbol |
| Graphite | `#101C24` | Dark background |
| Paper | `#F4F8F9` | Light surface and dark-background wordmark |

Color variants share exactly the same geometry. Do not create separate drawings
for the two backgrounds. Use `-light` assets on light surfaces and `-dark` on
dark surfaces. The white mark is for single-ink reversals. Keep the entire name
in one color; the G is not a separate product or suffix.

## Reproduce

```sh
python tools/repo/draw_branding.py
```

The generator uses Pillow and
`third_party/inter/docs/font-files/InterVariable.ttf`. Edit geometry, colors or
typography in that generator, then regenerate. It writes the shipping assets
to `branding/` and the overview to this directory. Vector marks use a single
SVG arc and a horizontal segment; the application SVG adds a rounded square. PNGs use the same coordinates
with supersampling. Lockups are PNGs so recipients do not need the font installed.

Every Windows ICO entry contains PNG data, including the small sizes. Preserve
this: the engine reads the embedded image with `stb_image` and does not support
DIB payloads in this path. Rebuild the executable to embed the updated ICO;
an existing installed executable is not modified by regenerating these files.

## Preview

- [Native-size application icon review](icon-review.png)
- [Identity overview](brand-board.png)
- [Gallery](index.html)
- [Shipping assets](../../../branding/README.md)

The social card is prepared locally; this does not update repository settings
or publish a new social preview automatically.
