# 0092 — A script lives in the instance it is put in

- Status: accepted
- Date: 2026-09-24
- Amends: [0057](0057-a-script-is-an-instance-and-the-editor-edits-one-thing.md)
  (a script is an instance and the editor edits one thing): its origin table
  keeps both rows, but `ScriptService` is no longer the only place a script can
  come from a file or go to one
- Relates to: [0050](0050-a-script-is-an-ordinary-instance-and-its-source-is-a-property.md),
  [0058](0058-a-script-runs-when-you-press-play.md)

## Context

The owner's words, from three reports on one day:

> meu amigo tentou criar um som em ScriptService e não conseguiu

> um script é uma instância, ou seja eu deveria conseguir adicionar qualquer
> filho — por exemplo uma estrutura de Script (Loader) e ModuleScripts filhos

> não deve ser baseado em uma única pasta onde tem todos os scripts, deve
> funcionar como script nas instâncias

ADR 0057 already made the runtime right: every enabled `Script` in the world
starts from its own `Source`, in document order, wherever it is. What was still
a folder model was everything **around** the runtime:

- the scene treated every `Script` under `ScriptService` as the mount's, so it
  did not save one, and it did not save what was inside one;
- the editor refused to make anything in `ScriptService` except a `Script`, and
  turned that `Script` into a new file under `src/scripts`;
- a `Script` dropped into `ScriptService` was written out as a file and
  re-mounted rather than moved;
- nothing could be put inside a script that came from a file, because the scene
  had nowhere to write it.

So a project's code had to live in one directory and one service, and a script
could not hold the modules it loads.

## Decision

**A script is an instance and lives where it is put.** Any instance goes into
any instance, a script included. A script made in the editor, in any service or
inside any instance, is saved in the scene with its `Source`, as every other
property is. `ScriptService` is an ordinary carried service.

**A script read from a file is marked as such, not recognised by where it is.**
The mount of `src/scripts` sets `World::mounted` on each `Script` it reads and
each `Folder` it makes to hold one. That flag is the only thing that decides
"the file is this instance's source":

- The scene does not write a mounted node whose whole subtree is mounted.
- A mounted node that holds something authored is written as a **mark**: class,
  name, `"mounted": true`, and the authored children. Reading finds the mount's
  node by class and name and puts the children back under it. A mark whose file
  has gone keeps its children in a `Folder` of that name, counted as
  `SceneIoReport::orphanedMounts`. They are somebody's work, and the file is
  not.
- `clearScene` walks into a mounted node instead of destroying it, so the
  file's script survives a scene change and only what was authored inside it is
  replaced.
- The editor does not move a mounted node, because the file decides where it
  is. It does take children, and those children move freely.
- Like `generated`, the flag travels in a snapshot and is not in the world
  hash. `Clone()` does not copy it: a copy of a file's script is a script of its
  own.

`src/scripts` stays supported. It is how a project written in an outside
editor, kept in git and hot-reloaded by `luaug dev`, brings its code in. It is
no longer the only way, and nothing in the editor sends a script there any
more.

## Consequences

- A `Loader` script holding `ModuleScript` children works in both kinds of
  script. In a scene script the children are saved with it. In a file's script
  they are saved as the mark's children.
- The editor loses the "make a Script in ScriptService, get a file" path and
  the "drop a Script into ScriptService, write a file" path. Both were the old
  folder model, and both lost data in the cases above.
- A mounted node is still deleted from the Explorer like any instance, and its
  file brings it back at the next open. Deleting or renaming the FILE from the
  editor is not part of this decision.
- Templates and examples keep their `src/scripts` layouts. They still work,
  and moving them is a separate change.
