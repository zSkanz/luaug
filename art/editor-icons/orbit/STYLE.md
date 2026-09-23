# Orbit — LuauG editor icon system

Created 2026-09-23. This is the active style for the default editor theme:
82 unique drawings serving 87 logical IDs. The application logo and branding
are a separate identity. Earlier PNG masters and briefs in the parent directory
are retained as historical sources; this specification supersedes their style.

## Exact design prompt

> Create the LuauG editor icon family in a style named Orbit. Design clean,
> compact geometric symbols with a calm, modern appearance. Use a 24 by 24
> coordinate grid, a consistent 1.8-unit stroke, round line caps and round joins.
> Prefer simple outlines with generous open interiors and recognizable silhouettes.
> Reserve solid areas for playback controls, small dots and cursor symbols.
> Center each subject optically and normally keep it within coordinates 2 to 22;
> allow small optical overshoots. Make the primary meaning legible at 16 pixels
> and preserve the silhouette at 13 pixels. Use one white ink on true transparency;
> colors are assigned by the editor, never painted into the source. No gradients,
> shadows, texture, bevels, lettering, decorative background tiles or tiny details.
> Related states share their base geometry: locked/unlocked, visible/hidden,
> script/module script. Distinguish classes by silhouette and meaningful interior
> marks. Use a conventional isometric wireframe only for spatial objects.
> For the stamp overlay use a solid circular base and the same circle with a
> transparent central hole. Deliver editable SVG geometry and 256 by 256 RGBA
> PNG masks, with pure white RGB even in transparent pixels. Check every icon
> on both light and dark backgrounds at 32, 24, 16 and 13 pixels.
>
> Subject: {logical ID and its meaning}. Preserve the established metaphor when
> adjusting an existing icon. Change only the requested characteristics and keep
> all shared family constants identical.

This prompt was the design specification for code-authored geometry, not a claim
of an image-model generation. No image model or external icon library was used.
The exact subject geometry is recorded in `draw_icon()` in
[`tools/repo/draw_icons.py`](../../../tools/repo/draw_icons.py).

## Reproduce or change

Run from the repository root:

```sh
python tools/repo/draw_icons.py
# Existing entry point also works:
python icons/bake.py
```

Requires the existing art-tool dependency Pillow. No engine dependency is added.
The generator reads the current theme manifest, keeps all IDs, aliases, roles,
palette and overlay settings intact, and emits:

- SVG sources under this directory, grouped by class, action, content and overlay.
- Runtime masks under `icons/default/`, using the existing file paths.
- `preview-dark.png` and `preview-light.png` with runtime-style BOX downsampling.
- `index.html`, a local gallery with a light/dark switch.

Edit the geometry in the generator, then regenerate. SVGs are portable editable
exports; direct SVG edits must be reflected in the generator before the next bake.
Stroke and grid constants apply to the entire family. Do not normalize individual
icons by cropping: that changes stroke weight. PNGs are rendered at 768 pixels
and reduced to 256 with Lanczos antialiasing; RGB stays white throughout.

## Review

Inspect both preview sheets. Check pairs at small sizes, inspect alpha bounds,
verify every manifest path and intentional alias, and confirm the overlay has
the same outer silhouette as its base. The gallery is monochrome; the PNG sheets
apply the actual role colors from the theme. The engine must be rebuilt/copied
and restarted to refresh an already loaded icon atlas.
