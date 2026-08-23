# 0052 — The content tree is what the project holds, and nothing in it runs

- Status: accepted
- Date: 2026-08-23
- Extends: 0047 (the world is data), 0050 (a script is an instance), 0051 (prefabs)

## Context

The human asked for `content/` to be more than a folder of files:

> a ideia do content é que ele seja tipo um explorer global entre todas as
> schenes saca essa é a principal ideia dele e da mesma forma que eu posso
> armazenar um prefab na workspace eu posso armazenar um prefab por exemplo no
> content

and, choosing between two shapes for it, picked a single tree of instances over
a file per thing.

Two decisions before it make this possible and make it worth having. A script is
an ordinary instance carrying its own source (ADR 0050), so a shared script is a
thing you can put somewhere. And a prefab is a definition instances inherit from
(ADR 0051), so a library of them is a library rather than a pile of copies.

## Decision

**`content/` gains a TREE, beside the files it already holds.** It is a
`scene::World` of its own with a `Folder` for a root, written to
`content/content.tree.json` in the scene format — the same writer and reader,
over a different root.

**Nothing in it runs**, and that is the whole difference between the two trees:
what makes a `Script` run is being in the WORLD when the world runs. A script in
the content tree is stored, exactly as a prefab there is stored.

**Instance inside instance, and the same verbs.** The plus, delete, rename,
duplicate, the properties grid and undo all work on it, because it is made of
instances and they are written against `scene::World` rather than against the
scene.

**Which tree is in front decides what a verb acts on**, and that is not a
drawing decision: a selection is a set of `InstanceId`s, and an id from one
world names an unrelated instance in the other. So switching tabs drops the
selection — there is no honest way to carry one across — and the flag lives on
the `Editor` rather than with the panel flags, because the frame loop needs it
too.

**It is written on change rather than at exit.** It is a thing two people edit
and a thing nothing else writes, so an editor that only saved it on a clean
shutdown would lose it exactly once — the same argument the remembered open
scene is written on change for.

## Consequences

- **The file browser stays.** A scene is a file and opening one is a file
  operation; the tree is for instances. Two panels, two questions, and neither
  pretends to be the other.
- **A project with no tree is not an error**, and every project written before
  this has none. It opens empty.
- **The tree shares the world's registries**, like a prefab stage: a `ClassId`
  is an index into a registry, so an instance could not move between the two
  otherwise — and moving between them is the point.
- **What is not answered here**: instancing a prefab that lives IN the tree
  rather than in a file. The seam exists — a `StampSource` is a name and some
  text — and pointing it at a subtree of the content tree is a small change; but
  what a stamp's NAME then is, and what happens when somebody renames the
  subtree it points at, are questions with no answer yet.
- **And nothing in the tree is reachable from a script yet.** `game` is the
  world; there is no global for the project's tree, deliberately, until somebody
  needs one — a name on the global list is the hardest thing in this API to take
  back (api-design.md §1.1).
