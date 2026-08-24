# Streaming a large world

Streaming plays a world larger than memory by keeping the part of it near a
**focus** resident and letting the rest go.

It is a service rather than a flag on the scene root, because it is a system —
it has foci, budgets, radii and events, and a boolean would not be enough of an
API to configure one.

## Turning it on

```luau
--!strict
local StreamingService = game:GetService("StreamingService")

StreamingService.MinRadius = 320     -- the promise
StreamingService.LoadRadius = 700    -- best effort
StreamingService.PauseOutsideLoadedArea = true
StreamingService:AddFocus(character)
```

`StreamingService.Enabled` is on by default. Turning it **off freezes** the
current set rather than draining it: nothing new is scheduled and nothing
resident is evicted.

## Foci

A focus is any instance with a position in the world — normally the character or
the camera. Several are legal, and a chunk any of them wants is kept.

`StreamingService.AddFocus` **refuses** an instance with no position rather than
silently anchoring the world at the origin.
`StreamingService.RemoveFocus` stops something being a focus; removing the last
one does not unload the world, it stops anything being wanted.

The **first** focus added is the primary one, and the floating origin follows
it. Not an average — an average between two characters walking apart is a point
neither of them is near, and it drifts every frame.

## The two radii

`MinRadius` is the **must-have** ring: everything inside it is guaranteed
resident before a focus may advance into it. `LoadRadius` is the **best-effort**
ring: a chunk inside it is wanted, and one outside it stays resident until it
passes a wider ring still.

The gap between them is hysteresis, and it is the point. A single radius makes a
character standing on a boundary load and evict the same chunk every frame, and
the symptom is not a wrong world — it is a stutter nobody can find.

`StreamingService.PauseOutsideLoadedArea` decides what happens when the minimum
ring is not yet resident. It is a **pause, not a rollback**: the tick simply
does not advance.

## Loading somewhere else

```luau
--!strict
StreamingService:LoadAreaAsync(destination, 200)
character:PivotTo(CFrame.new(destination))
```

`StreamingService.LoadAreaAsync` yields until every chunk within the radius is
resident. It is what a teleport calls **before** it moves the character. It
returns immediately if the area is already loaded, and raises rather than
hanging when the world has no chunks there.

## Being told

```luau
StreamingService.AreaLoaded:Connect(function(position: vector, radius: number)
    hideLoadingScreen()
end)

StreamingService.InstanceStreamedOut:Connect(function(instance: Instance)
    -- a husk: reparented to nil, not destroyed
end)
```

`StreamingService.InstanceStreamedOut` fires **per instance that left the world
while a script still held a reference**. Streamed out means **reparented to
`nil`, not destroyed** — the handle still resolves, and reading it is legal. An
instance nothing referenced is simply destroyed and does not fire this.

That distinction is what makes the event worth having: it is the engine telling
you that a reference you are holding now points at something outside the world.

## Authoring a streamed world

**You build in `Workspace` and press play.** There is no generator script, no
sorting things into folders, and nothing to configure: the engine partitions
your scene into cells on the way to the first frame, cached by a hash of the
scene file, and a shipping build warms the same cache so a player's first launch
pays nothing.

```luau
--!strict
local StreamingService = game:GetService("StreamingService")
StreamingService:AddFocus(workspace.CurrentCamera)
```

That is the whole of it. Place four hundred parts across a kilometre, save, run.

### What a `Model` decides

The grid decides **when** something becomes eligible. A `Model` decides **what
comes with it**, through `Model.StreamingMode`:

| Mode | What it does |
|---|---|
| `Nonatomic` | The default. Its parts are placed in cells one at a time, by their own positions, and they arrive and leave independently. |
| `Atomic` | The model is one unit. It goes in a single cell however far it spreads, and it materialises and evicts whole — a house arrives as a house rather than as forty parts in an order nobody chose. |
| `Persistent` | It never enters the grid. It stays in the scene, it exists before the first tick, and no eviction reaches it however far you walk. |

`Atomic` is what a gate, a machine or a building wants. `Persistent` is for the
spawn, the checkpoint, and anything a script holds a long-lived reference to.

### What stays authored, and why it says so

The partition is deliberately conservative. An instance leaves the scene only
when a cell can express it whole, nothing else in the scene points at it by
path, and it carries no descendant a cell cannot say. Everything else stays in
the scene — which costs memory and never costs correctness.

So these stay: anything that is not a `Part` or a `MeshPart`; a part with
children; a part carrying an attribute; a part something else names by path (a
`Weld`'s `Part0`, `Workspace.CurrentCamera`); an `Atomic` model with a light or
a script inside it; and a stamped instance whose root is not an atomic model.
**To stream a stamp, make its root a `Model` and set it `Atomic`.**

### Size classes

A cell also has a **class**, chosen by how large the thing in it is:

| Class | Extent | Radius |
|---|---|---|
| detail | under 12 m | `MinRadius` / `LoadRadius` |
| structures | 12 m to 24 m | `StructureMinRadius` / `StructureLoadRadius` |
| terrain features | over 24 m | `TerrainMinRadius` / `TerrainLoadRadius` |

A mountain and a pebble stop sharing a distance, which is the choice a single
grid forces and always resolves badly in one direction. The three extra pairs
are **zero by default and mean "follow `MinRadius` and `LoadRadius`"**, so a
world that says nothing about them behaves exactly as it always did.

An `Atomic` model is classified by the whole model's extent, not by its largest
part: a house of forty small parts is a structure and not forty details.

### Address by tag, never by path

**This is the one thing a streamed world changes about how you write a script.**

`workspace.Bridge.Plank` is a path that is sometimes `nil`, because the thing it
names may not have arrived. Waiting for it is not the answer either —
`WaitForChild` on something that is not streamed in never returns.

Ask [`TagService`](api:TagService) instead:

```luau
--!strict
local TagService = game:GetService("TagService")

for _, plank in TagService:GetTagged("Plank") do
    -- everything wearing the tag that is here NOW
end

TagService:GetInstanceAddedSignal("Plank"):Connect(function(plank: Instance)
    -- a cell arrived and brought this with it
end)

TagService:GetInstanceRemovedSignal("Plank"):Connect(function(plank: Instance)
    -- and one left
end)
```

A streamed instance carries the tags it was authored with, so those two signals
are exactly what fires as cells come and go. That is the primary way to find
things in a world that is not all present.

### Compiled cells, for a world a generator makes

A world that is GENERATED rather than authored still has the path M7 shipped: a
`*.chunk.json` cell under the content directory, compiled by
`luaug build-assets`.

```json
{"format":"luaug-chunk-source","chunkSize":256,"x":0,"z":0,"layer":0,
 "minY":-10,"maxY":40,
 "instances":[
   {"kind":"part","name":"Ground","anchored":true,
    "position":[0,0,0],"size":[256,1,256],"color":[0.3,0.4,0.3]},
   {"kind":"meshpart","mesh":"asset://models/tree.glb",
    "position":[12,0,-4],"size":[1,1,1]}
 ]}
```

- Every cell in one world must agree about `chunkSize`, or the compiler refuses.
- A `meshpart` with no `mesh` is **refused**: an invisible part would surface
  later as "the world is missing things" rather than as an error now.

The two coexist. A cell a compiled world already owns is one the partitioner
leaves alone, and whatever was going there stays in the scene rather than
overwriting it.

## Budgets

Chunks are loaded in score order — distance from the nearest focus — with the
score as the IO priority.

**The per-frame materialisation budget is time, not a count of chunks**, because
a chunk's cost varies with what is in it. Materialisation runs until the budget
is spent and resumes next frame. That is what turns "the world loaded" from a
hitch into a few milliseconds spread over several frames.

## Watching it

The debug overlay draws a **streaming map**: one grid per size class, a square
per cell, coloured by state — green resident, blue loading, yellow decoded and
waiting for budget, red failed, grey unloaded. Press **F3** in a running game.

It is the fastest way to answer the two questions streaming actually raises:
how far the ring reaches, and whether something you cannot see is missing or
merely far away.

## Where to look next

- [Floating origin](manual:assets/floating-origin) — the other half of a large
  world
- [The asset pipeline](manual:assets/pipeline)
- [`StreamingService`](api:StreamingService)
