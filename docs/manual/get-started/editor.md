# The visual editor

```bash
luaug edit examples/06-scene
```

**With no project named — `luaug edit` on its own, or the engine
double-clicked — you get the project browser first.** It lists what you have
opened before, marks anything that has moved, opens a folder you point it at, and
makes a new project from a template. Choosing one starts the editor on it.

The browser is not a separate application either, and neither is what it starts:
a project decides the content it mounts, the scripts it runs and the layout its
panels remember, all of which are read once when a project opens. So choosing one
starts a fresh editor rather than reloading the one you are looking at.

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

## Moving around and the keys

Click into the viewport and **W, A, S, D fly, Q and E go down and up**, with
Shift to go faster and the wheel to change speed. Hold the right button to
look around. The keys fly only while the viewport has focus, and never while
Ctrl or Alt is held, so a shortcut never moves the camera.

| Keys | Do |
|---|---|
| Ctrl+1 | Select, with no handles on the selection |
| Ctrl+2 / Ctrl+3 / Ctrl+4 | Move / scale / rotate handles |
| Ctrl+L | Switch the handles between world and local axes |
| Alt (while dragging) | Suspend snapping |
| Alt+click | Select the part itself, not the model it is in |
| Double-click | Open a model, to select what is inside it |
| F | Frame the selection |
| Escape | Leave a terrain or block brush, then close a model, then deselect; in play, stop |
| Ctrl+D, Delete, F2 | Duplicate, delete, rename |
| Ctrl+G / Ctrl+Alt+G | Group into a Model / into a Folder |
| Ctrl+Shift+G or Ctrl+U | Ungroup |
| Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+Shift+V | Copy / cut / paste beside / paste into |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Shift+P (playing) | Fly free: leave the game's camera and fly the editor's while the game runs |

Picking up a terrain or block brush lets go of the selection, and selecting
something (in the Explorer, or by inserting it) puts the brush down.

Above the viewport, the **Home** tab is the toolbar with the snap steps beside
the snap switch; **Model** inserts a part, model, folder or script and groups
and duplicates; **Test** plays, pauses, steps and flies free; **View** shows and
hides panels and overlays.

## Where things can go

Every instance in the Explorer takes a child from its **+** -- whether the child
does anything there is another matter -- and the scene saves what is inside
every service, not only `Workspace`. **A script goes wherever you put it**, and
is saved in the scene with its `Source` (ADR 0092): inside the part it drives,
in a model, or in `ScriptService`, which is an ordinary service.

Two things are not the scene's to save. Streamed chunks belong to the streaming
system. And a script mounted from a file under `src/scripts` belongs to its
file: the editor shows it but does not move it, and anything you put inside it
is saved as yours.

## The script editor

A script opens with the caret on its first line. `(`, `[`, `{` and quotes close
themselves; a closer typed where one stands steps over it; a bracket or quote
typed over a selection wraps it. Suggestions are accepted with Enter, Tab or a
click. Type annotations are coloured as types. An error says what is wrong at
the end of its line, and in full under the pointer.

| Keys | Do |
|---|---|
| Ctrl+/ | Comment or uncomment the line, or every selected line |
| Shift+Alt+A | Block comment around the selection, or take it out |
| Ctrl+Space | Offer suggestions |
| Ctrl+Left / Ctrl+Right (with Shift: select) | Move by word |
| Ctrl+Backspace / Ctrl+Delete | Delete the word to the left / right |
| Ctrl+C / Ctrl+X with nothing selected | Copy / cut the whole line |
| Ctrl+Shift+K | Delete the line or the selected lines |
| Alt+Up / Alt+Down | Move the line or selection |
| Alt+Shift+Up / Alt+Shift+Down | Copy the line or selection up / down |
| Ctrl+Enter / Ctrl+Shift+Enter | Open a line below / above |
| Tab / Shift+Tab over lines, Ctrl+] / Ctrl+[ | Indent / outdent |
| Ctrl+L | Select the line, then the next |
| Ctrl+D | Select the word, then its next occurrence |
| Ctrl+Shift+\ | Jump to the matching bracket |
| Ctrl+F / Ctrl+H / Ctrl+G | Find / replace / go to |
| Ctrl+S | Save the script |

Suggestions read the file's own code as well as the engine's API: a table the
file fills in (`Snake.` offers `new` and `Grow`), `self` inside a method, a
value annotated with a type the file declares (`type Snake = { Body: ... }`),
and a list type's element (`self.Body[1].` offers a part's members).

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

- **Stop restores the world, not the script VM.** A script that mutated its own
  module state carries that across a stop.

## Where to look next

- [Scenes: the world as data](manual:world/scenes)
- [The world is data, scripts are behaviour](manual:why/world-is-data)
- [The luaug CLI](manual:get-started/cli)
