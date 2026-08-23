# The visual editor

```bash
luaug edit
luaug edit examples/06-scene
```

The editor is not a separate application. It is a **mode of the engine**: the
same binary, the same frame loop, the same world. What changes is that while the
editor is editing, the tool owns the machine — the tick, the cursor, the audio,
the camera and the keyboard — and pressing play hands them back.

## The panels

A dockspace you can rearrange, and the layout is remembered per project.

| Panel | Is |
|---|---|
| **Viewport** | The world, rendered into a texture. Click to select, fly to move. |
| **Explorer** | The instance tree. |
| **Properties** | Everything the selected instance declares, typed. |
| **Content** | The project's assets, as folders. |
| **Console** | What the running game has said. |
| **Stats** | The same numbers the debug overlay shows. |

## The loop

This is what the editor is for, and it is worth stating as a sequence:

1. Open a scene from the **content** panel. A scene is one of the assets in the
   project, so that is where scenes are opened from.
2. Click something in the viewport. The explorer highlights it and the
   properties panel fills.
3. Change something — a colour, a size, a position.
4. Press **play**. The world ticks; scripts run.
5. Press **stop**. The world goes back to exactly where you pressed play, **with
   your change still in it**.
6. Press **save**. The scene you have open is rewritten — that one, not a fixed
   name.

Step 5 is the one that matters. A tool where testing your work costs you your
work is one nobody uses twice.

## Three run states, not two

`Editing`, `Playing` and `Paused`. Three rather than two, because "editing" and
"paused in play mode" are not the same state — and a two-state model makes the
play button a toggle between things that are not opposites.

**Play, pause, step, stop.** Step advances exactly one simulation tick, which is
the tool for a bug that only happens on one frame.

## Selecting and editing

Clicking in the viewport casts a ray and selects what it hits. The selection is
outlined in the viewport, revealed in the explorer, and expanded in the
properties panel.

The properties panel is generated from the same API definition the reference
pages are, so it knows a property's type, its enum, whether it is read-only, and
whether it is **stored and not yet acted on** — and it says so rather than
letting you click something that will do nothing.

## Undo

Undo and redo, over property edits and over structure. Coalesced, so dragging a
value is one entry rather than sixty.

## The content browser

The project's `content/` directory, as a tree, with folders and right-click
menus. Scenes live there — `content/scenes/<name>.scene.json` — addressed by URN
and resolved like any other asset, which is what makes "a project with scenes"
different from "a project with a scene".

Creating a folder, renaming, deleting and duplicating are all there.

## Where the world comes from

An authored world is data. `[project] scene` in `luaug.toml` names the one a run
starts with, and the editor saves back to whichever one is open — see
[Scenes: the world as data](manual:world/scenes).

Code-first is not deprecated by any of this: `Instance.new` at runtime stays
first-class, exactly as it is in the other engines that work this way. What
moved is where the world a project *starts* with is written down.

## What it does not have yet

The editor is being built in phases, and this is the honest state of it:

- **Manipulators** — dragging a translate, rotate or scale handle in the
  viewport — are in progress. Editing a transform today means typing numbers in
  the properties panel.
- **Multi-select**, creating an instance from the viewport, and reparenting by
  drag are in the same phase.
- **Stop restores the world, not the script VM.** A script that mutated its own
  module state carries that across a stop.

## Where to look next

- [Scenes: the world as data](manual:world/scenes)
- [The world is data, scripts are behaviour](manual:why/world-is-data)
- [The luaug CLI](manual:get-started/cli)
