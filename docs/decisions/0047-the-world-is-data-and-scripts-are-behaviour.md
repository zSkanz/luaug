# 0047 — The world is data and scripts are behaviour

- Status: accepted
- Date: 2026-08-22
- Supersedes: — (changes what ADR 0017 deferred, and reframes ADR 0045's
  "a packaged game is a folder that ships source")

## Context

**Human decision, 2026-08-22**, taken at E1's review and in these words: an
engine without stop is not an engine — how are we going to edit, test and save?
— and then, asked which of three shapes "save" should take: *the way I want is
how the big engines do it, Unity, Unreal.*

The question was forced by the editor's first review and it could not have been
forced earlier, because until something could show a world and change it, nobody
had to say where a change goes. In LuauG today a world does not exist on disk. In
`examples/10-open-world` the terrain is 289 chunks of data and **everything else
is built by `init.luau` every boot** — the character, the lantern, the welds, the
camera, the input contexts, the HUD. So an editor that moves a part has nowhere
to put the move: the next boot rebuilds the world from the script, and the edit
is gone.

Three shapes were on the table and two of them are traps.

**A scene file the engine loads with the scripts still building on top** is cheap
to write and expensive to live with: the script creates a tower, the file has one
too, and now there are two. Two sources of truth for one world is a defect
generator, not a design.

**The editor writing back into the Luau source** keeps one source of truth and is
fragile in a way that never improves — the editor would have to understand code a
person wrote, and it would be wrong about it forever.

## Decision

**A project's authored world lives in data. Scripts describe behaviour.**

- A LuauG project gains a **scene**: a file describing an Instance tree — its
  hierarchy, its classes, its properties, its attributes and its tags. That file
  is the source of truth for the world a project starts with.
- **Boot loads the scene, then starts the scripts.** Not "run the scripts and
  see what world they build". The lifecycle in `docs/api-design.md` §3 gains a
  step in front of it, and `game.Loaded` keeps meaning what it means.
- **The editor is the authoring surface for that file**, and the only one that
  has to exist. A text format a person can also read and diff is a property
  worth having, not a second interface to maintain.
- **Play and Stop are a snapshot and a restore of the loaded world.** Press play
  and the editor remembers the world it has; press stop and it puts it back. This
  is the Unity and Unreal semantic and it is the only one that makes "test" a
  step in a loop rather than a thing you do by relaunching.

**Code-first does not die and is not deprecated.** `Instance.new` at runtime
stays exactly what it is — a game that spawns enemies, streams chunks, or builds
its whole world in code remains a first-class LuauG game, the same way a Unity
game may create GameObjects at runtime. What changes is where the world a project
*starts* with is written down. `examples/01-instances` and the streamed chunks of
`examples/10-open-world` are both still correct programs after this ADR.

## Consequences

**What this makes possible**, and it is the whole reason: edit → test → stop →
save becomes a closed loop inside one window. That loop is what an editor *is*;
without it the editor is a viewer with a properties grid.

**What it costs.**

- A scene format has to be designed, and the strongest candidate is already
  written: `engine/scene/src/world_hash.cpp:182-278` walks every instance in slot
  order, class by name, parent, children in sibling order, attributes, tags, and
  every property of every superclass through the generated accessors — a
  deterministic, ordered, complete traversal with a `Hasher` where a writer
  should be. Its three warnings (never emit a `NameAtom`'s number, never an
  unordered container's order, never struct bytes) are the correctness spec of a
  stable file format, written before anybody needed a file.
- **The engine cannot write a file at all.** `engine/platform/include/luaug/platform/file.h`
  reads and never writes. That is a new platform capability, and it is one the
  game VM must NOT be given: R4 keeps `io` out of the sandbox, and an editor
  writing a scene is the host writing, never a script.
- `World::snapshot()` is named in five comments across `engine/scene` as the
  reason every component is trivially copyable, and **it does not exist**.
  ADR 0016 called these "foundations, not rollback". Play and Stop are the first
  caller those foundations ever had.
- **`luaug-chunk-source` must not become the scene format by drift.** Its own
  header calls it "deliberately NARROW — this is not a general scene
  serialization", it cannot express a rotation, a hierarchy, an attribute or any
  class beyond `Part` and `MeshPart`, and it is grid-bound. The two formats meet
  at streaming and stay two things.
- **`luaug build` and ADR 0045 need re-reading**: a packaged game ships source
  today, and after this it also ships a world.
- Hot reload's meaning narrows and improves: reload the behaviour, keep the
  world. That is closer to what a person means by it.

**What this ADR does not decide**, deliberately, because each is a milestone's
own question answered from a position that has seen the previous one: the
format's syntax and whether it is one file or many; how a scene references
assets; whether prefabs are scenes; conflict and merge behaviour; and what
happens to a project that has both a scene and a script that builds a world —
which is legal and must have a stated answer rather than an emergent one.
