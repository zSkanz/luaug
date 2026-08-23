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

A chunk is a cell of a uniform grid, 256 metres on a side by default, authored
as a `*.chunk.json` file under the content directory and compiled by
`luaug build-assets`:

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
- `layer` exists from the start, for interiors or a coarser level of detail.

Both streamed examples in this repository **generate** their worlds from a
deterministic hash of each cell's own coordinates. A hundred lines of generator
rather than megabytes of committed JSON, and the world is identical on every
machine.

## Budgets

Chunks are loaded in score order — distance from the nearest focus — with the
score as the IO priority.

**The per-frame materialisation budget is time, not a count of chunks**, because
a chunk's cost varies with what is in it. Materialisation runs until the budget
is spent and resumes next frame. That is what turns "the world loaded" from a
hitch into a few milliseconds spread over several frames.

## Watching it

The debug overlay has a **Streaming** panel:

```luau
--!strict
local DebugService = game:GetService("DebugService")
DebugService:ShowPanel("Streaming")
DebugService.OverlayVisible = true
```

## Where to look next

- [Floating origin](manual:assets/floating-origin) — the other half of a large
  world
- [The asset pipeline](manual:assets/pipeline)
- [`StreamingService`](api:StreamingService)
