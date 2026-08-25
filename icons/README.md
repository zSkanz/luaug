# icons/ — the editor's icon themes

The runtime form of the editor's icons. `art/editor-icons/` is where they are
*drawn* and reviewed; this is what the engine *loads*.

**This directory is data. No code here.** What the engine has to do with it is
specified below so it can be built without reading the art brief.

## Where it goes at build time

Same shape as the two content sources that already exist:

```
i18n/en.json    ->  content/i18n/en.json          engine/app/CMakeLists.txt:112
shaders/        ->  content/shaders/              cmake/luaug_shaders.cmake
icons/          ->  content/icons/                to be added
```

`platform::paths().contentDir` is `executableDir/content`, so a theme resolves
to `content/icons/<themeId>/theme.json`. A plain recursive copy is enough —
these are finished PNGs and nothing needs compiling.

**Do not route these through `assetc`.** It sends every image to basisu for
KTX2/UASTC with generated mips, which is right for a world texture and wrong
here: block compression smears small alpha shapes, and a 16-pixel icon is
nothing but small alpha shapes. Copy them.

## A theme

A theme is any directory containing a `theme.json`:

```json
{
  "id": "default",
  "name": "LuauG Default",
  "version": 1,
  "size": 256,
  "tintable": true,
  "fallback": "class.Instance",
  "icons": {
    "class.Part": "class/Part.png",
    "content.Folder": "class/Folder.png",
    "action.List": "class/UIListLayout.png"
  }
}
```

| Field | |
|---|---|
| `id` | the directory name, and how a project or plugin names this theme |
| `size` | the square edge of every PNG in it. All 256 here |
| `tintable` | the images are **masks**: white everywhere, meaning in the alpha. Multiply by a theme colour at draw time |
| `fallback` | the id to draw when a lookup misses. Must exist |
| `icons` | logical id to path, relative to the theme directory |

**Paths repeat on purpose.** Five ids point at another id's file — a browser
`Folder` and a tree `Folder` are one drawing, and a `.glb` in the content folder
is what a `MeshPart` points at. Two names for one file, never two files that are
the same icon and drift apart the first time one is touched.

## Colour: `palette`, `roles`, and the one rule that governs them

Every icon is tinted, and **the tint says what KIND of thing it is, never which
thing.** The shape carries the identity; the colour only makes a long tree
scannable. That distinction is not decorative — it is the same rule that decided
the engine's own mark, and it is what makes everything below safe.

```json
"defaultRole": "neutral",
"palette": { "spatial": { "light": "#184E88", "dark": "#A7CAEF" }, ... },
"roles":   { "class.Part": "spatial", "action.Delete": "danger", ... }
```

`roles` names an id's palette key. **An id absent from `roles` takes
`defaultRole`** — thirty of the seventy-eight are, and they are the toolbar
verbs, which are chrome. Four verbs are not: `Play`, `Pause`, `Stop`, `Delete`.
A toolbar of ten colours is a fruit salad; a toolbar where the destructive one is
red is a tool.

**Ten categories, not seventy-eight colours.** Seventy-eight is noise. Ten is
something a person learns in a day and then reads without looking, and it is what
every editor that does this well does.

### Two values per role, and why it is not two decisions

`light` and `dark` are the value on a light panel and on a dark one. A single
colour cannot serve both: clearing 3:1 against a near-white panel wants luminance
under 0.175 and clearing it against the dark panel wants over 0.11, and
everything inside that band is muddy.

They are **solved from one hue**, not picked twice. That is what stops them
drifting — nobody chooses the second value by eye.

### Every value clears WCAG, and the palette survives colour blindness

The floor anywhere in the palette is **3.88:1**, against the 3:1 WCAG 1.4.11 asks
of a non-text graphic.

More importantly, the roles are separated by **lightness as well as hue**, and
which role gets which lightness was chosen by search rather than by eye — the
objective being to maximise the smallest distance between any two roles across
normal vision and all three dichromacies at once. Hand-ordering it reached a
worst-case separation of 8; the search reaches 28.8.

That matters because **no ten hues stay distinct under deuteranopia**, which
about one man in twelve has. Hue alone would have collapsed `spatial` into `ui`
and `script` into `spatial`. With the lightness ranks, the palette degrades to a
legible greyscale instead of to mush.

**Tinting must be switchable off**, and when it is, every icon takes the panel's
own foreground. The set was drawn and collision-checked in one ink before any
colour existed, so the uncoloured editor is not a degraded one.

### How a plugin recolours things

`palette` and `roles` are both sparse overlays, exactly like `icons`. A plugin
that wants one class in its own colour adds a palette key and points that id at
it:

```json
"palette": { "myCompany.Rig": { "light": "#7A1F5C", "dark": "#E48CC4" } },
"roles":   { "class.CharacterBody": "myCompany.Rig" }
```

No file, no drawing, four lines. And because a role is a *name* rather than a
hex, a plugin that only wants a different green overrides `palette.script` once
and every scripting icon moves together.

## The id namespace

Three groups, and the prefix says which question the icon answers:

| Prefix | What it names | Count |
|---|---|---|
| `class.` | a class the Explorer can show — the name is the class name exactly | 42 |
| `action.` | something a person clicks: toolbar, panel buttons, tree chevrons | 24 |
| `content.` | a `ContentKind` from `app/content_tree.h` | 6 |
| `overlay.` | a badge drawn ON TOP of another icon — see below | 2 |

### `overlay.` — a badge, and it is drawn differently from everything else

An overlay is not an icon in a row. It is a mark in the corner of one, and it is
the only namespace whose ids are **not** drawn on their own.

```json
"overlay": { "scale": 0.40, "haloScale": 1.22, "corner": "bottom-right" }
```

**Two ids, drawn in this order, every time:**

1. `overlay.StampBase` — the badge's outer silhouette, solid, with no interior
   detail — drawn in **the panel's own background colour** at
   `scale * haloScale` of the icon's edge. This punches a clean hole in whatever
   is underneath.
2. `overlay.Stamp` — the same silhouette with the mark cut out — drawn in the
   foreground at `scale`, centred in that hole.

**The knockout is not a refinement, it is what makes the badge exist.** Measured
across the class set at 16 px: **37 of 42 icons already have ink where the badge
goes** — up to 51% under `class.Workspace` and 49% under `class.Folder`. A bare
badge lands on a folder's body and disappears.

**The two files share an outer silhouette exactly** — zero pixels of difference,
because the mark is the base with a hole punched in it rather than a second
drawing of the same shape. One pixel of disagreement shows as a rim on one side.
They are produced by a script for that reason: it is a precision requirement, and
an image model cannot meet one.

**The silhouette is a circle, and that is the load-bearing choice.** Most of the
class set is rectangular — `Frame`, `TextButton`, `ScreenGui`, `Part`, `Model`,
`UICorner`. A rounded-square badge in the corner of a rounded rectangle joins the
family: composited over `class.Folder` it reads as a notch bitten out of the
corner rather than as a mark placed on top. A circle never does that on any icon
at any size. **At 7 px the hole inside is two pixels and its shape does not
survive — only the outline does**, which is why the outline is the decision.

`scale` was chosen by sweeping 0.34 to 0.52 composited over the real icons at
16 px: below 0.40 the hole closes, above it the badge starts eating the subject.

An overlay takes `defaultRole` like anything else, so it inherits the panel's
foreground. If a badge ever needs to be told apart from the icon it sits on,
give it its own palette role — do not tint it by the subject's role, because a
badge means the same thing everywhere.

`class.` ids are the generated class names, so a lookup is mechanical:
`"class." + instance.className`. Likewise `content.` maps straight off the
`ContentKind` enum.

## How a lookup resolves — and this is what makes plugins work

In order, first hit wins:

1. **A project override** — `<project>/.luaug/icons/<id>.png`. One file, no
   manifest. This is how somebody replaces exactly one icon without shipping a
   theme.
2. **Plugin themes**, in load order. A plugin ships a directory with a
   `theme.json` and **only the ids it overrides** — a partial theme is legal and
   is the normal case.
3. **The active theme**, if a project names one.
4. **`default`**, which is complete by construction.
5. **The `fallback` id** of whichever theme was consulted last.

The rule that makes this cheap: **a theme does not have to be complete.** Only
`default` does. Everything else is a sparse overlay, so overriding one icon
costs one file and four lines of JSON, and a theme cannot break the editor by
forgetting an id.

Resolution belongs in one function that takes an id and returns a texture. If
that function is the only thing that knows about themes, adding a plugin source
later is adding a case to a list rather than touching every call site.

## What the loader has to do

**Downscale once, at load, and cache.** These are 256 px and the Explorer draws
at 16, 20, 24 and 32. Resampling per frame is waste; resampling with a poor
filter is a smudge. Box or Lanczos to the sizes actually used, into an atlas.

**One atlas, not seventy textures.** Sixty-seven files at four sizes is a lot of
binds for a tree that redraws every frame. Pack them; ImGui takes a native
texture handle and UVs (`debug_overlay.cpp:724` already draws one).

**Tint at draw time.** RGB is white everywhere and the meaning is in the alpha,
so one file serves both the light and the dark panel. There is no `d_` set and
there must not be one — two drawings of the same icon drift.

**A generated registry is worth it.** This repository already generates C++ from
data (`api/generator/gen_cpp.luau` writes the class descriptors). An icon id
enum generated from `default/theme.json` turns a missing icon into a build error
and a typo into a compile failure, instead of a blank square at runtime. It also
makes the class list and the icon list unable to drift apart silently.

## What is not here yet

**Nothing is missing.** All 78 ids resolve to a master, and no two ids that are
meant to be different icons share a file by hash. The six that were absent while
this file was first written — `Delete`, `Save`, `Open`, `New`, `Rename`,
`Refresh` — are in.

No redraw is outstanding. `action.Rotate` was the last one and it landed
centred, so every id in the theme is the drawing it is meant to be.

**The smallest size any of these is drawn at is the toolbar's**, which is
`GetFrameHeight() - FramePadding.y * 2` in `debug_overlay.cpp` — the font size,
13 px at ImGui's default. That is the number to check a new icon against, not
16: below about 20 px an arrowhead stops being a head on every circular icon in
the set, and what has to survive is the silhouette underneath it.

## Re-baking

The masters live in `art/editor-icons/`. The bake reads the keyline for each
icon **out of the art brief's own tables** rather than from a copied list, so a
row that changes keyline changes the output on the next run and the two cannot
drift.

Each master is levelled (clamping JPEG ringing and any grey haze to the ends
while keeping real edge antialiasing), trimmed to its shape, scaled to fit its
keyline exactly, centred on 256, and written as alpha.

That last step is the one worth knowing about: **every icon here sits exactly on
its keyline**, which was never true of any master — they came back anywhere
between 89% and 112%. A folder and a cube now look the same *size* rather than
having the same number of pixels, and that is what makes a column of them read
as one set.
