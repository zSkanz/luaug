# What LuauG is

A standalone game engine, scripted in Luau, with a C++ core.

If you have written Luau before, the shape will be familiar within a minute: an
`Instance` tree, `game:GetService`, `task.spawn`, signals with `:Connect`,
`Vector3` and `CFrame`. That is deliberate — it is a developer experience worth
keeping.

What is underneath it is not familiar, and that is deliberate too.

## What it is made of

- **The Luau VM, embedded directly** in a C++20 core. Not a binding layer over a
  scripting language chosen later.
- **A data-oriented ECS** behind the `Instance` facade. The tree is what you
  write against; contiguous component pools are what iterate.
- **A deterministic fixed tick.** The simulation runs at a fixed timestep with a
  stated determinism guarantee, and rendering interpolates between ticks.
- **A custom rendering interface** over the platform's own graphics API, with
  cascaded shadows, clustered lights, image-based lighting from the sky, and a
  post chain.
- **Jolt** for 3D physics, behind an engine interface — no backend type reaches
  a script.
- **Chunk streaming with a floating origin**, for worlds larger than memory and
  larger than 32-bit floats are comfortable with.
- **Sub-second hot reload**: save a file and the world is rebuilt around you.

## What that buys you

**A world that reproduces.** Same build, same platform, same seed, same inputs,
same result — verified by hashing the simulation and replaying recorded input.
That turns "it happened once" into a test.

**A surface you can trust the analyzer about.** Every class, property, method
and event is declared once in a typed definition, and the same source produces
the engine's registration, the type definitions the analyzer reads, and these
reference pages. If a member is on those pages, the engine has it.

**Nothing hidden.** No hidden camera controller, no hidden character state
machine, no hidden input mode. A camera is an instance your script moves.

**Your own backend.** There is no hosted platform behind this, which is a
smaller promise and a portable one.

## What it is not

- **Not a Roblox clone**, and not compatible with Roblox content. The concepts
  are borrowed; the code, assets and semantics are not. Several familiar
  spellings are deliberately different, and each difference has a reason written
  down.
- **Not a hosted platform.** No accounts, no data stores, no matchmaking.
- **Not multiplayer yet.** There is no replication in this release. The fixed
  tick and the determinism guarantee are the foundations it will rest on.
- **Not mobile yet.** Desktop first.

## The honest gaps

Worth knowing before you start rather than after:

| Missing | State |
|---|---|
| Particles, decals, terrain, world-space UI | Planned, in a later phase |
| Rich text | Not scheduled |
| Constraints beyond a rigid weld | Not scheduled |
| `BasePart.Material` | Not shipped; a surface look rather than body state |
| A filesystem for scripts | Not present; persistence is a backend |
| Shadows from point and spot lights | Stored and not yet acted on |
| A concave mesh collider | Accepted and behaves as a convex hull |

Each of those has a page saying what exists instead.

## Where to look next

- [Install and toolchain](manual:get-started/install)
- [Your first world](manual:get-started/first-world)
- [The migration guide](manual:roblox/migration), if you are arriving with
  habits
- [The API reference](site:reference)
