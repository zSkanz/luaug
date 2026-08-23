# 0049 — A Stamp is a source, an instance carries its mark, and editing it breaks the mark

- Status: accepted
- Date: 2026-08-23
- Implements: 0048 (content is the source, an instance is a link to it)
- Extends: 0047 (the world is data and scripts are behaviour)

## Context

ADR 0048 wrote down the model in the human's own words — content holds sources,
instancing one into the world makes a linked instance, editing that instance
breaks the link — and deliberately left three things open: what the thing is
called, what "editing" means exactly, and how a linked instance is written to a
file. E3 cannot be built without answering all three, and answering them while a
browser is being wired is how a link gets designed twice.

## Decision

### It is called a STAMP

Chosen by the human from four candidates, and the reasons are worth keeping
because they are the reasons to say no to the obvious names:

- **`Prefab` is Unity's, `Blueprint` is Unreal's, `Model` is Roblox's, and an
  instanced `Scene` is Godot's.** Borrowing one imports a mental model this
  engine has not agreed to — a Unity prefab has nested variants and an override
  list that grows forever, and somebody who reads "prefab" will look for those.
- **A stamp is a noun and a verb**, which no other candidate managed: you stamp
  one into the world. The instance is *stamped*, what it came from is *its
  stamp*, and an edit *breaks the stamp*.
- One syllable, and it survives being said out loud in a room.

A stamp is a file: `content/stamps/<name>.stamp.json`. The extension pairs with
`.scene.json` on purpose — they are the same format over a different root, which
is the next decision.

### A stamp file is a scene of one subtree

`scene_file.h` already writes a complete, ordered, deterministic description of
a subtree, because that is what a scene is. A stamp reuses it whole: same
writer, same reader, same four correctness rules (a class by name, a name as
text, no struct bytes, no `InstanceId`), and a `root` that is one instance
instead of `Workspace`.

Writing a second format would mean two definitions of "everything about a
subtree", and they would disagree the first time somebody added a property —
which is the argument `scene_file.h` already makes about the world hash, applied
one level down.

### An instance carries its mark, and the mark is not part of the simulation

`InstanceRecord` gains a stamp name beside `generated`, and it is the same kind
of fact: **what a person wrote down**, not what the simulation is. So, exactly
like `generated`:

- it travels in a `WorldSnapshot`, so undo and stop keep it;
- it is **not in the world hash**, because a hash asks what the simulation is
  and a stamped part ticks like any other. Two worlds that differ only in where
  their parts came from are the same world to a solver, and R10 is about the
  solver.

### What a stamped instance writes to a scene, and it is very little

**Its stamp, its name, and its transform. Nothing else, and none of its
children.** The children belong to the stamp file; writing them again would be
the full copy that makes a prefab worthless, and it would go stale the moment
the stamp changed.

This falls out of the break rule rather than being a second decision: if any
other edit breaks the link, then a linked instance CANNOT have any other
override, so there is nothing else to write.

### What "editing" means — the question 0048 left open

**Everything inside a stamped subtree breaks the mark, except the root's own
`CFrame` and its `Name`.**

- The transform is the instance's own. This is Unity's answer and it is right
  for the same reason: placing a thing is not changing the thing. Four lamp
  posts in four places are four lamp posts, and if moving one made it stop being
  a lamp post the model would be useless for the case it exists for.
- The name is the instance's own for the same reason — `LanternPost_Left` is
  where it is, not what it is.
- **Everything else breaks it**: a property write, a child added or removed, a
  reparent into or out of the subtree, a delete of any descendant. Not an
  override list that grows forever, because that is a mechanism nobody can
  predict; and not a change that propagates back to the file, because that
  silently edits every other copy.

**Breaking is not a failure and the editor says so plainly.** A broken instance
is a normal instance: it serialises in full, it can be stamped again, and
nothing is lost.

### Only the editor breaks a mark

`World::setProperty` does not break anything. A game that moves, colours or
destroys a stamped instance at runtime is playing, not authoring, and a link
that dissolved because a script ran would be a link nobody could rely on — and
would make a world's contents depend on how long it had been running.

The break happens in `Editor`, on the authoring path, which is the same place
that already decides what may be created, deleted and reparented.

## Consequences

- **Changing a stamp changes every unbroken instance of it**, on the next load.
  That is the whole point, and it is also the part that will surprise somebody
  the first time: the editor has to show which instances are stamped, and it
  does — the Explorer marks them.
- **A stamp of a stamp is not addressed here.** Nesting one stamped subtree
  inside another raises questions this ADR does not answer (does the outer file
  record the inner link? does breaking the outer break the inner?), so E3 refuses
  it outright rather than half-answering it: making a stamp out of a subtree that
  already contains a stamped instance is refused, with a message.
- **`content/stamps/` is a convention, not a rule.** The file's kind is in its
  name (`.stamp.json`), the way `.scene.json` already works, so a project may
  organise them however it likes.
- **A stamped instance's children are not editable in place**, because editing
  them is what breaks the mark. That is the model working, and the editor's job
  is to make the consequence visible before the click rather than after it.
