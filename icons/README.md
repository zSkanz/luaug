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

## The id namespace

Three groups, and the prefix says which question the icon answers:

| Prefix | What it names | Count |
|---|---|---|
| `class.` | a class the Explorer can show — the name is the class name exactly | 42 |
| `action.` | something a person clicks: toolbar, panel buttons, tree chevrons | 24 |
| `content.` | a `ContentKind` from `app/content_tree.h` | 6 |

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

Six `action.` ids have no file: **`Delete`, `Save`, `Open`, `New`, `Rename`,
`Refresh`**. They are drawn tomorrow. They are absent from `theme.json` rather
than present and broken, so a loader that honours `fallback` behaves correctly
today.

`class.UICorner` is present but is being redrawn — it collided with
`action.Stop` at 2.0%, the closest pair this set has produced.

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
