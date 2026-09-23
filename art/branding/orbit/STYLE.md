# Orbit G — LuauG visual identity

Created 2026-09-23 in response to the request to refresh the engine logos in
the same visual family as the Orbit editor icons. This replaces the previous
crescent artwork in `branding/`. Earlier candidates and derived files remain
historical references.

## Exact design prompt

> Refresh the LuauG game engine identity to match the Orbit editor icon family.
> Build an original geometric symbol from an open circular orbit that reads as
> a capital G, with a short inward horizontal bar and one detached circular
> satellite above the upper-right shoulder. Use a 64-unit square, a 7-unit
> uniform stroke, rounded endpoints and joins, and generous negative space.
> Keep the mark flat, compact and recognizable at 16 pixels. Use no crescent
> inside the ring, gradients, bevels, shadows, texture, extra outlines or tiny
> details. The detached satellite and the G must remain distinct in one ink.
> Pair the symbol with the exact name "LuauG", preserving the capitalization.
> Set the name in the vendored Inter typeface at weight 650, optical size 32,
> with tracking of -2.5% of the em. Use one weight and one color across the name.
> The visual character is precise, rounded, calm and contemporary, with mint
> and deep teal accents, graphite typography and an off-white surface.
> Produce transparent horizontal and stacked lockups for light and dark
> backgrounds; colored, white and graphite versions of the standalone mark;
> a 1280 by 640 social card; and application icons at 16, 24, 32, 48, 64, 128
> and 256 pixels. The social card may use restrained orbital lines in the
> background. Keep those lines outside the actual logo. Preserve a monochrome
> version and inspect the smallest icon sizes on light and dark backgrounds.

This is the exact design specification used for authored vector geometry and
typesetting. No image model was used. Typography comes from the existing
OFL-licensed Inter font; no external artwork or font dependency was introduced.

## Geometry

- Canvas: 64 by 64 units.
- Orbit: center `(30, 33)`, radius `21`, stroke `7`.
- Start angle: `-55` degrees; arc travels counterclockwise through 305 degrees
  to `(51, 33)`. The horizontal bar ends at `(37, 33)`.
- Satellite: center `(54, 10)`, radius `4.5`.
- Rounded caps and joins; minimum clear space around a placed mark is one
  stroke width. Do not stretch, rotate or independently recolor the satellite.
- Minimum application icon: 16 pixels. The stroke is optically heavier than
  a toolbar glyph because the mark must stand alone as an application identity.

## Palette

| Token | Value | Use |
|---|---|---|
| Primary | `#17BDA6` | General symbol and OS icon |
| Deep teal | `#07867C` | Symbol on light backgrounds |
| Orbit mint | `#55E0C5` | Symbol on dark backgrounds |
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
SVG arc, a horizontal segment and a circle. PNGs use the same coordinates
with supersampling. Lockups are PNGs so recipients do not need the font installed.

Every Windows ICO entry contains PNG data, including the small sizes. Preserve
this: the engine reads the embedded image with `stb_image` and does not support
DIB payloads in this path. Rebuild the executable to embed the updated ICO;
an existing installed executable is not modified by regenerating these files.

## Preview

- [Identity overview](brand-board.png)
- [Gallery](index.html)
- [Shipping assets](../../../branding/README.md)

The social card is prepared locally; this does not update repository settings
or publish a new social preview automatically.
