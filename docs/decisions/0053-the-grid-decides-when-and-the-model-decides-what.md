# 0053 — The grid decides *when*, the model decides *what*, and partitioning is the tool's job

- Status: accepted
- Date: 2026-08-23
- Extends: 0047 (the world is data and scripts are behaviour)
- Relates to: 0048 (content is the source), 0049 (a stamp is a source),
  0050 (a script is an ordinary instance), 0051 (a prefab is inherited),
  0052 (the content tree is what the project holds)

## Context

M7 shipped chunk streaming and it works: a uniform 256 m grid, an index with
every cell's real bounds, two radii with hysteresis, a millisecond budget, and
`examples/10-open-world` walking 4.35 km of it. What it does not have is a way to
*author* a streamed world.

Today the flagship's 289 cells come from `tools/generate_world.luau` — a script
that computes a height field and writes 289 `.chunk.json` files, which `assetc`
compiles. The editor has never seen that terrain and could not save it if it
tried: **the scene serializer skips a `generated` subtree whole**, deliberately,
which is why `main.scene.json` is 5 KB rather than tens of megabytes.

So there are two worlds in one project that do not speak to each other. The
authored one is small and comfortable; the streamed one is large and only a
script can make it. Asked how to build a big world by hand, the honest answer
today is "you cannot", and that is the gap this closes.

The question that opened it was asked plainly:

> vamos supor que eu construi um mundo enorme na workspace como que isso vai
> virar chunk?

And then, having looked at how the other engines do it:

> eu acho que ter a tecnologia das outras engines más com um formato mais puxado
> pro do roblox talvez fique bom

That is the decision below, and the second half of it is what makes the first
half small.

### What the survey found

**Unreal 5's World Partition** partitions one authored level into a runtime grid
automatically, with One File Per Actor so the editor loads only what is near and
source control never collides on a level file. Each actor carries
`Is Spatially Loaded`. Cells are driven by *streaming sources*, of which the
player is one.

**Godot's Open World Database**, a community addon, does something none of the
big engines do: it chooses a cell by the object's **size**, not only its
position. Small props live in small cells with a short radius; terrain features
live in large cells with a long one. It is half of a LOD hierarchy for none of
the cost of one.

**Roblox** streams around the player with a min and a target radius, and — the
part that matters here — makes the **`Model`** the unit rather than the part.
`ModelStreamingMode` is `Nonatomic` (parts stream individually), `Atomic` (the
model arrives and leaves as one), `Persistent` (never streams, sent at join), or
`PersistentPerPlayer`.

**Guerrilla's Decima** is the scale check: streaming, memory and the asset
pipeline for Horizon Zero Dawn is a named role held by one engineer for twelve
years. What that budget buys, beyond the grid, is baked distant geometry — and
nothing in this ADR attempts it.

### What the engine already has, which is more than expected

Four of the five things Roblox exposes are already here and none needs to change:

| Roblox | LuauG |
|---|---|
| `Workspace.StreamingEnabled` | `StreamingService.Enabled` |
| `StreamingMinRadius` | `MinRadius` |
| `StreamingTargetRadius` | `LoadRadius` |
| `StreamingIntegrityMode` | `PauseOutsideLoadedArea` |
| `Model.ModelStreamingMode` | — **this is the gap** |

`Enabled` is better than its Roblox counterpart and the reason is written in the
IDL: turning it off **freezes** the resident set rather than draining it, because
evicting while refusing to load empties the world one ring at a time and leaves
nothing to come back to.

Two more things were found in the format and both were assumed missing:

- **`ChunkId` already carries a `layer`**, documented since M7 as "how interiors,
  or a lower level of detail of the same ground, get addressed without a second
  coordinate system". Nothing has ever used it.
- **`ChunkIndexEntry` already carries a world-space `DAABB`**, so a building that
  overhangs its cell is already described correctly and already scored correctly.
  The straddling problem was solved before it was asked.

## Decision

**A world is partitioned automatically. The grid decides WHEN something becomes
eligible; the `Model` decides WHAT comes with it. A person places things and
never sorts them.**

Six rules.

### 1. `Model.StreamingMode`, with three values

Roblox's enum minus the one that needs a network.

- **`Nonatomic`** — the default. Parts descend into cells individually, each by
  its own position. This is how the flagship's world already behaves, so the
  default changes nothing that exists.
- **`Atomic`** — the model is one unit: it materialises whole and evicts whole,
  across a cell boundary if it spans one. A house arrives as a house rather than
  as forty parts appearing in an order nobody chose.
- **`Persistent`** — never enters the grid at all. It stays in the scene and
  always exists. The spawn, the sun, the camera.

`Persistent` is what a `SpatiallyLoaded` boolean would have been, and the enum
says strictly more for the same typing. **`PersistentPerPlayer` is deliberately
absent**: it needs a per-connection notion of a player, which is post-v1 phase 4's
to introduce.

### 2. The partitioner is arithmetic, and it runs at play

Walk `Workspace`, compute `floor(position / chunkSize)`, group by cell honouring
each model's mode, write. There is no heuristic, no tuning, and nothing for a
person to configure.

**It runs on play, cached by a hash of the scene.** Not only at build: the path
somebody uses all day is the one that must not go stale, and a build step is a
thing to forget. A shipping build pre-warms the same cache; it is not a second
code path.

### 3. A cell holds groups, not only parts

The one file-format change. A chunk's payload gains a group: an atomic model and
its descendants, materialised together. `ChunkId.layer` and `ChunkIndexEntry`'s
bounds are untouched.

This is what dissolves the objection that a chunk is a flat list of
`Part | MeshPart` and therefore cannot hold a light, a sound, or a model with
children. With the model as the unit, it can.

### 4. Cells are chosen by size as well as position, on the existing `layer`

An object's cell layer follows its extent:

| layer | what | radius |
|---|---|---|
| 0 | detail — props, small vegetation | short |
| 1 | structures | medium |
| 2 | terrain features | long |

A mountain and a pebble stop sharing a radius, which is the choice a single grid
forces and always resolves badly in one direction. `StreamingFocus` gains a
`minRadius`/`loadRadius` pair per layer.

**This is the only change to the existing streaming runtime.**

### 5. Address by tag, never by path

A streamed world makes `workspace.Bridge.Plank` a path that is sometimes `nil`,
and Roblox's own documentation records that `WaitForChild` on an unstreamed
instance hangs forever. The answer is not to wait better.

`TagService` already has the three methods this needs — `GetTagged`,
`GetInstanceAddedSignal`, `GetInstanceRemovedSignal` — and **the two signals are
exactly what fires as cells arrive and leave**. This ADR makes that the
documented primary path rather than a technique somebody discovers.

### 6. Authority is world state, and a cell is content

The multiplayer design approved 2026-08-21 says a `World` is authoritative or a
replica and that there is no server build. Streaming follows from that without a
special case:

- an **authoritative** `World` streams around the **union** of its players' foci;
- a **replica** streams around its own player's focus;
- **solo** is an authoritative world with one focus, which is today.

`StreamingFocus` is already a list and `setFoci` already replaces it wholesale,
so four players are four foci and **nothing in the policy engine changes for
multiplayer**.

**The wire never carries world geometry.** Both sides read the same cell files
from their own disk; only what is dynamic is replicated. That is the structural
difference from Roblox, where the server replicates instances, and it is why an
authoritative world does not have to hold the whole map resident the way a Roblox
server does.

One behaviour differs and is decided here: **`PauseOutsideLoadedArea` pauses the
simulation, and a replica cannot pause a shared server** because its own ground
has not arrived. A replica holds its camera and shows that it is loading; the
pause remains the authoritative world's.

## Consequences

**A person builds in `Workspace` and presses play.** That is the whole point, and
everything above exists to make it true without the engine ever holding the world
resident: the partitioner streams the scene through a bucketing pass rather than
materialising it.

**The default is what already happens.** `Nonatomic` on a world of loose parts
partitions exactly the way `generate_world.luau` does by hand. Nothing that runs
today changes behaviour.

**The scene serializer's `generated` skip stays, and stays correct.** A
materialised cell is not authored content. This ADR adds a path *from* authored
content *to* cells; it does not make cells savable.

**A path reference may be `nil`, and that is now documented rather than
discovered.** Rule 5 is a real change to how a person writes a script for a
streamed world, and the honest framing is that it is the cost of a world that is
not all present — the same cost every engine with streaming charges.

**Distant geometry is still absent.** Outside `LoadRadius` there is nothing, not
a cheap version of something. Layers (rule 4) push the horizon out for large
objects and buy real distance, but they are not HLOD, and HLOD is not in this
ADR. It is named as the next wall, with Decima's twelve years as the estimate,
so that nobody plans a 13 km world believing this closes it.

**Editing is still bounded by the editor, not by the grid.** The world must fit
in the editor to be edited, because the scene is one file and the editor holds
all of it. The third step — a scene that is a *folder* of per-cell files, with
streaming while editing, as Unreal's One File Per Actor does — is deliberately
not attempted. That wall appears far later than the one this removes.

**Opening a stamp on top of a streamed world is an open seam, and it is named
here rather than discovered.** `clearScene` deliberately skips a `generated`
subtree — "a new scene is not a reason to evict the ground a streaming system put
there" — and ADR 0049's stamp session builds the stamp after that clear. So a
stamp opened in a streamed project appears **standing on the terrain**, which is
the opposite of the isolated stage a prefab mode is for.

`StreamingService.Enabled = false` does not fix it: it **freezes** the resident
set rather than draining it, which is the right semantics for gameplay and
exactly the wrong one here. There is no existing way to ask for an empty stage.

Whichever milestone reaches it first owns it, and the shape is not in doubt:
**entering a stamp evicts the streamed set and leaving brings it back**, the way
the stamp session already snapshots and restores everything else. What needs
deciding is only whether that is a third state on the service or a call the
editor makes directly — and it should be decided once, with the stamp's stage,
rather than twice.

### Rejected

**A `SpatiallyLoaded` boolean, from Unreal.** Superseded by rule 1: the enum
expresses the same exclusion as `Persistent` and also expresses grouping, which
the boolean cannot.

**Partitioning at build only.** Rejected because the daily path would then be the
one that goes stale, and a forgotten build step is a class of bug that reports
itself as a broken world.

**Partitioning by materialising the world first.** The obvious reading of
"automatic at play", and it defeats the purpose: holding every instance in order
to decide which ones to hold is the exact cost streaming exists to avoid. The
partitioner buckets records as it reads, so its peak is one record.

**Growing the chunk format to hold arbitrary hierarchy.** Rule 3 adds groups,
which is what atomic models need and no more. A general nested-instance
serialization is `.scene.json`'s job and duplicating it in a payload read
thousands of times per session is a cost with no caller.
