# `examples/03-physics-playground` — the M5 deliverable

A world with mass, and a capsule you steer through it. Everything that moves
here is moved by the simulation, not by a script writing `CFrame` every frame —
that is the whole difference between this example and `01-instances`.

Run it:

```
luaug-host examples/03-physics-playground                     # windowed
luaug-host examples/03-physics-playground --headless --frames=180 --exit --screenshot=out.png
```

## Controls

| Key | What it does |
|---|---|
| `W` `A` `S` `D` | walk, relative to where the camera is looking |
| `Space` | jump, when grounded — ignored in mid-air rather than queued |
| `Left` `Right` | turn the camera |
| `Up` `Down` | raise and lower it |

Every one of them goes through **`KeyboardService`**, which is M5's scaffold and
exists for exactly one milestone. ADR 0029 makes the Input Action System the
only input model; this service is tagged `DevOnly` so it cannot reach a shipped
game, and migrating this file to the IAS is an M6 gate item.

## What it is showing

| In the picture | The thing being exercised |
|---|---|
| Three towers of crates | dynamic bodies in contact, sleeping when they settle and waking when hit |
| The shallow ramp | a slope inside `MaxSlopeAngle`: ground you can walk up |
| The steep ramp beside it | a slope outside it: a wall that happens to be tilted |
| The kerb | `AutoStepHeight` — the one number that separates a character from a capsule |
| The wall behind the kerb | what happens when a step is taller than that number |
| The plank on the fulcrum | a seesaw with no constraint in it: a contact and a centre of mass |
| The red marker over the capsule | a `Weld`, so it is driven by the character and never simulated |
| The capsule itself | `CharacterBody` on a Jolt `CharacterVirtual`, not a rigid body |

## What it looks like, and why

**Everything renders as a wireframe box**, because a `Part` has no geometry of
its own in this release: only a `MeshPart` reaches the solid renderer, and a
`Part` is drawn by the debug path (`render_world.cpp`, "Debug parts"). That has
been true since M2 and M5 did not change it — the milestone's subject is what
the world does, not what it looks like.

It is a real gap rather than a decision, it is recorded as **D022** in
`docs/defects.md`, and it is scheduled with M7.5 ("Looking Like an Engine"),
which is the milestone that owns how this engine renders. Nothing you see here
is the physics failing to work: the boxes are where the bodies are.
