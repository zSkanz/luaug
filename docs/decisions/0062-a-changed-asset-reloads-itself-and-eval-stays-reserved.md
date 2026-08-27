# 0062 — A changed asset reloads itself, and `eval` stays reserved

- Status: accepted
- Date: 2026-08-27
- Extends: [0024](0024-hot-reload-fast-world-restart.md) (hot reload is a fast world restart)
- Relates to: [0010](0010-asset-stack.md) (the asset stack),
  [0012](0012-networking-v1-primitives-only.md) (networking is v1 primitives only)

## Context

The dev protocol reserved two verbs at M3 and implemented neither. The brief
said why, and the two reasons were not the same kind of reason:

> **Asset and shader hot swap.** The protocol reserves `asset-changed`; nothing
> consumes it, because **no asset pipeline exists before M4/M7**. Wiring a
> message no receiver acts on is not the same as implementing the feature.
>
> **The `eval` dev console.** The message type is reserved in the protocol and
> rejected by the engine with a not-implemented key. **Running arbitrary source
> in a live world touches R4 and deserves its own design.**

One is a schedule. The other is a design question. M4 and M7 have both shipped,
so the first reason has expired — and a reserved verb whose stated blocker no
longer exists is a verb nobody will revisit, because the note explaining it
still reads as current.

## `asset-changed`: built

**The implementation is a removal**, and it is that small because the loaders
already load everything MISSING. `syncTextures` reads every map it cannot find
and `syncPrimitives` does the same for meshes, so making an entry missing *is*
the reload. There is no second path to write and no state machine to get wrong.

`MeshLoader::forget` takes the texture out of `TextureLibrary`, destroys its
handle, removes the mesh from `MeshLibrary` and releases its buffers from
`MeshCache`. The next frame's sync reads the file as it stands on disk.

**It works because loose content is not cached.** `ContentMounts::resolve`
answers a loose URN with a *path* and reads nothing — D039 took the read out of
it, when every streamed chunk was being read twice — so the next load opens the
current file. A URN served from a pack or from the editor's object store
resolves to bytes the mount keeps, and forgetting one reloads the same bytes.
That is correct rather than a limitation: those are compiled artifacts, and
changing one means recompiling it.

**The watcher answers a different verb for content than for source**, which is
the distinction that makes this worth having. A `.luau` under `src/` is a script
and reloads the world; a `.png` under `content/` is an asset and reloads itself.
One verb for both would restart the game every time somebody saved a texture,
which is the opposite of what a hot reload is for.

**Content is snapshotted by size and modification time, not by contents.** A
project's scripts are kilobytes and comparing them directly cannot collide; its
content is megabytes, and reading all of it on every filesystem event to decide
whether anything changed would cost more than the reload it is deciding about.

**A request that lands mid-flight is left alone.** A deferred read that
completes after a forget writes the old bytes into a fresh entry, and the next
forget drops that too — so the worst case is one stale frame. The alternative is
cancelling work from the middle of a pipeline whose whole design is that nothing
holds a pointer into it.

## `eval`: still reserved, and this says what it is waiting for

The design question the M3 brief named has not been answered, and it is two
questions rather than one.

**R4, the sandbox.** `eval` would compile and run source the engine did not load
through its own module resolver, inside the game VM, with the globals table
sealed. That is not automatically unsafe — the chunk would be sandboxed like any
other — but it is a second door into the VM, and the first thing anybody would
want from a dev console is the ability to reach things a script cannot. A design
that grants that has to say what it grants and how a shipping build refuses it.

**R10, determinism.** This is the harder half and the brief did not name it. The
world hash is a pure function of the operation sequence, and a replay re-runs
that sequence. An `eval` that mutates the world inserts operations the recording
does not contain, so a replay of a session in which anybody typed into the
console cannot reproduce it — silently, because nothing marks the trace. Any
design has to choose between: refusing `eval` while a recording is active,
recording the evaluated source into the trace as an operation, or accepting that
a console session is not replayable and saying so where somebody would find out.

None of those is hard. All of them are decisions, and taking them without the
console being asked for would be inventing requirements.

**What would change this:** somebody wanting the console. The verb answers
`engine.dev.err.not_implemented` with the requested type in the message, which
is what makes the reservation visible rather than silent, and this record is
what stops the next reader concluding that the M3 blocker still applies to both
verbs when it applies to neither.

## Consequences

- Editing a texture in an external tool updates the running game, without
  restarting it and without losing the world's state.
- `render_world_tests.cpp` holds the removal's exactness — one URN, not its
  neighbours — and that a URN nothing loaded is a no-op rather than a
  diagnostic, which is the ordinary case: a watcher reports every save, and most
  are for content no frame has asked for.
- The dev protocol has one reserved verb left, and the reason it is reserved is
  written down where somebody deciding to build it will look.
