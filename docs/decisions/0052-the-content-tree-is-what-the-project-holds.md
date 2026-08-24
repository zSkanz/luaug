# 0052 — The content tree, and why it was taken out the same day

- Status: **reversed** (accepted and reversed 2026-08-23)
- Extends: 0047 (the world is data), 0050 (a script is an instance), 0051 (prefabs)

## What was decided

`content/` was given a TREE beside the files it already held: a `scene::World`
of its own with a `Folder` for a root, written to `content/content.tree.json`,
global to every scene — a place for prefabs, shared scripts, anything wanted
once rather than once per scene. Nothing in it ran. It had its own panel, its own
save path, and a drag that copied subtrees between it and the world.

It was built, tested and shipped, and taken out the same afternoon.

## Why it was reversed

The human asked one question:

> más faz sentido ter esses 2 em unity e unreal n tem né?

**No, they do not.** Unity has one Project window: the folder tree of `Assets/`,
and a prefab is a file in it. Unreal has one Content Browser: `/Content`, and a
Blueprint is a file in it. Neither has a second global tree of instances, and
neither wants one.

And the fault is in how the choice was put. The options offered were "a file per
thing (like Unity and Unreal)" and "a single tree of instances", as though they
were two shapes of one idea. They were not: the second one's own description
carried its costs — *one file everybody edits, and a prefab that is no longer
something you can send someone* — and both were built anyway. Two panels called
"content" is what that looked like on screen, and it is what prompted the
question.

**Everything else in this milestone already assumes a file.** A stamp IS a file;
`Instance.stamp` reads a file; the override diff reads a file; the prefab stage
opens a file. The tree was the one piece that did not fit, and it brought a
second store, a second save path and a second answer to "where does this live".

## What replaced it

Nothing new — the thing that was already there. `content/` is a folder, the
`content` panel browses it, and a prefab is a file in it. What the tree was
reaching for is served by the drags:

- **Drag an instance from the Explorer into the browser** and it becomes a
  prefab file, in the folder it was dropped in, named after the instance. That
  is what dragging from the hierarchy into the Project window means in every
  editor that has both.
- **Drag a prefab out of the browser** onto an Explorer row to place it there,
  or onto the viewport to place it under `Workspace`.

## What this is worth keeping for

The decision is wrong and the record of it is not. Two things came out of the
day that outlive it:

- **`Editor::Stage` exists** because the tree needed a second world first, and
  the prefab stage — which is right, and is Unity's prefab mode — is built on it.
- **`World::classes()` and friends are non-const** for the same reason, which is
  what lets the serializer build a reference tree to diff a stamped instance
  against (ADR 0051).

And the lesson is about the question rather than the answer: **an option whose
own description lists its costs is an option to argue against, not one to offer
neutrally.** A person choosing between two things they have not built yet is
choosing on the framing, and the framing was mine.
