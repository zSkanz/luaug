# Orbit — LuauG editor icon system

Created 2026-09-23. This is the active style for the default editor theme:
138 unique drawings serving 150 logical IDs. The application logo and branding
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

## Coverage extension

Added the built-in network, player, particle, terrain, voxel, 3D GUI and
constraint classes. Abstract/base classes reuse the corresponding family
silhouette. Dedicated selection, terrain operation and clipboard symbols
replace unrelated action icons in the editor.

Subject prompt extension: use connected nodes for networking, a particle plume
for emitters, mountain contours for terrain and stepped cells for voxels. Use
a cursor for selection, arrows over a ground line for raise/dig, an even line
for flatten, a brush for paint, and conventional clipboard/scissors/document
symbols. Preserve Orbit's 24-unit grid, rounded strokes and white alpha masks.

Validation: all 71 classes in the generated engine reflection descriptors
resolve to a declared icon. All 150 IDs resolve to existing 256 x 256 RGBA
white masks with nonempty alpha. Dark/light sheets cover 32/24/16/13 px.

Further action subjects: Import uses an arrow entering a tray; NewFolder adds
a plus to the folder; Tools uses a wrench/tool silhouette; Information uses
a circled i. PlaceBlock, BreakBlock and ReplaceBlock share a cube silhouette
with plus, minus and replacement-arrow marks. These are wired to Content,
Tools, About and the Blocks panel, retaining text labels and tooltips.

Latest action-extension validation: PNG mask checks and Windows app/platform/
editor-shell suites passed. The Linux host build was blocked by unrelated
`-Wdouble-promotion` errors in scene_file.cpp (terrain cell settings); no full
Linux test pass is claimed for this extension.

2D layer extension: Part2D is a flat outlined shape with an origin marker and
an offset plane corner; Tilemap2D is a stepped arrangement of square tiles.
Use the spatial role, shared rounded strokes and existing alpha-mask format.
Both resolve directly from reflected class names in the Explorer and pickers.

2026-09-24: Tiles-panel extension. Erase uses a diagonal eraser silhouette;
View2D uses a flat frame with perpendicular axes. Keep Orbit geometry and masks.
Tiles tabs/menus use Tilemap2D; Paint reuses the brush.

Tiles-extension validation: all 128 manifest IDs have valid 256px RGBA masks;
preview inspected at 32/24/16/13 px. The Windows interface object compiled.
The complete build was blocked by undefined asI32 calls in replication's
extract.cpp, outside this icon change. The subsequent Linux host build and targeted
app/platform/editor-shell tests passed.

NavigationService extension (2026-09-24): draw a route from an outlined start
point through two waypoints to an arrow indicating the destination. Use the
motion role and preserve Orbit's 24-unit grid, stroke and alpha-mask rules.
The Explorer resolves it directly by the reflected class name.

Material-assets extension: keep the surface sphere for content.Material (alias
of the established material art). NewMaterial adds a plus to that silhouette;
MaterialVariant connects a parent surface to a derived surface. Use the spatial
role and the same rounded white strokes. Wire the two actions to the browser's
New Material and New Variant menus.

Refresh refinement: use two opposing open circular arcs with arrowheads
connected to their endpoints. Keep clear gaps between the arrows and an open
center so the refresh silhouette survives at 13/16 px. Do not reuse Rotate's
single arc or add disconnected arrowhead marks.


Script and material actions extension (2026-09-24) ? exact drawing prompt:
> Extend Orbit on its existing 24-unit grid, with 1.8-unit rounded strokes,
> white RGBA masks and no embedded text. Replace shows source text lines and
> a right arrow leading to replacement lines; ReplaceAll adds a second route
> into those lines. StepOver arches over a stopped point, StepInto points
> down toward that point, StepOut points upward away from it. Revert uses a
> return arc around a value line. Inherit connects a parent box to a child box
> with a downward elbow arrow. Keep every action neutral and distinguish the
> three debugger steps by silhouette. Inspect at 32, 24, 16 and 13 pixels on
> light and dark surfaces. Retain labels on replacement and debugger controls;
> compact close/revert/inherit controls have descriptive tooltips.

Seven new drawings: Replace, ReplaceAll, StepOver, StepInto, StepOut, Revert,
Inherit. Existing Play, Close, Save, Undo, Redo and Copy are reused. The theme
now resolves 139 IDs to 127 drawings. [Focused review](script-actions-review.png).


## Planned lighting classes ? 2026-09-24

Source: [accepted ADR 0096](../../../docs/decisions/0096-atmosphere-post-effects-and-a-sky-are-instances-under-lighting.md)
and its [implementation plan](../../../docs/briefs/atmosphere-post-kickoff.md).
Seven concrete classes plus the abstract PostEffect base; these assets reserve
names in the icon theme, not new classes in the runtime API. Explorer lookups
will resolve them by class name once those classes are implemented.

Exact authored-vector prompt:
> Extend the Orbit family with eight lighting icons on its 24-unit grid,
> using 1.8-unit rounded white strokes and transparent RGBA masks. Atmosphere
> is a horizon dome above three receding haze layers. Sky is a framed sky
> with a small sun and a cloud arc. BloomEffect is a four-point glow with
> separated diagonal rays. ColorCorrectionEffect is a circle split between
> a clear half and a hatched half. BlurEffect is a central circle with three
> short horizontal diffusion marks on each side. DepthOfFieldEffect is a
> sharp central circle inside four focus corners. SunRaysEffect is a sun
> in the upper left emitting three long divergent shafts. PostEffect is
> two offset image layers with a processing curve on the front. Use the
> existing light palette role; no baked-in color, gradients, letters or new
> dependencies. Keep each silhouette distinct from Lighting, PointLight and
> SpotLight. Review all eight at 32/24/16/13 pixels on light and dark surfaces.

[Focused lighting review](lighting-review.png). Reproduce with
`python tools/repo/draw_icons.py`, then `lute tools/repo/genicons.luau`.
Lighting-extension total: 147 IDs, 135 drawings, including these eight class IDs.


## Navigation extension ? 2026-09-25

Source: [ADR 0098](../../../docs/decisions/0098-navigation-agent-types-areas-links-crowds-and-the-plane.md).
NavigationArea, NavigationLink and NavigationAgent are registered by their
planned class names; their implementation remains independent of the artwork.
Explorer resolves the class IDs through its existing name-based lookup.

Exact authored-vector prompt:
> Extend Orbit with NavigationArea, NavigationLink and NavigationAgent. Keep
> the 24-unit grid, 1.8-unit rounded strokes, white transparent RGBA masks and
> the existing motion palette used by NavigationService. NavigationArea is
> a diamond-shaped ground region subdivided by crossed diagonals, with a
> lower contour suggesting its volume. NavigationLink is two outlined endpoint
> circles joined visually by an overhead jump arc with an attached arrowhead.
> NavigationAgent is a walking figure with a separate destination arrow in
> the upper right. Preserve open negative space and distinct silhouettes;
> no letters, gradients, textures or new dependencies. Compare with the
> existing NavigationService at 32, 24, 16 and 13 pixels on both themes.

[Navigation review](navigation-review.png) includes the existing service for
comparison and is regenerated by `python tools/repo/draw_icons.py`.
Regenerate C++ IDs with `lute tools/repo/genicons.luau`.
Total after navigation: 150 IDs and 138 drawings.

## Teams extension -- 2026-09-25

Source: [ADR 0099](../../../docs/decisions/0099-teams-and-network-ownership.md).

Exact authored-vector prompt:
> Extend Orbit with Team and TeamService in the system palette that Player and
> NetworkService use. Team is a flag on a pole: a vertical staff with a
> swallow-tailed pennant at its top. TeamService is two Player silhouettes side
> by side, heads and shoulders overlapping slightly, standing on one base line.
> Keep the 24-unit grid, 1.8-unit rounded strokes and white transparent masks;
> compare with Player at 32, 24, 16 and 13 pixels on both themes.

Regenerate with `python tools/repo/draw_icons.py`, then `lute tools/repo/genicons.luau`.
Current total: **152 IDs and 140 drawings**.

[Teams review](teams-review.png) compares Player, Team and TeamService at
32/24/16/13 pixels on both themes; regenerated by the same artwork script.
