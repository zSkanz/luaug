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
- **A deterministic fixed tick.** The simulation runs at a fixed timestep and
  gives the same result on every platform it ships on, and rendering
  interpolates between ticks.
- **A custom rendering interface** over the platform's own graphics API, with
  cascaded shadows, clustered lights with shadows of their own, image-based
  lighting from the sky or a skybox, materials as assets, an atmosphere, and a
  post chain of effects you add as instances.
- **Jolt** for 3D physics and a 2D physics library for the plane, behind engine
  interfaces: no backend type reaches a script. Joints, welds and ragdolls are
  instances.
- **Worlds of every size**: chunk streaming with a floating origin for worlds
  larger than memory, voxel terrain you dig and sculpt, block worlds, and a
  navigation mesh built where agents ask for one.
- **A 2D layer**: sprites, tilemaps and an orthographic camera, in the same
  world as 3D.
- **Multiplayer**: one authority, replicas that predict their own character and
  interpolate everyone else, and `RemoteEvent` and `RemoteFunction` for your own
  messages.
- **A visual editor** that is a mode of the engine: the same binary and the
  same world, with a script editor that type-checks as you write.
- **Sub-second hot reload**: save a file and the world is rebuilt around you.

## What that buys you

**A world that reproduces.** Same build, same seed, same inputs, same result,
and since level C, the same result on Windows, Linux and macOS. It is verified
by hashing the simulation and replaying recorded input, which turns "it
happened once" into a test, and it is what lets a replica and its authority
agree.

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
- **Not a hosted platform.** No accounts, no data stores, no matchmaking. A match
  is hosted by a player's machine or by a server you run.
- **Not mobile or web yet.** Desktop first: Windows, Linux and macOS.

## The honest gaps

Worth knowing before you start rather than after:

| Missing | State |
|---|---|
| A filesystem for scripts | Not present; persistence is a backend |
| A concave mesh collider | Accepted and behaves as a convex hull |
| Shaders you write yourself | Designed (ADR 0091), not built |
| An HDR panorama sky, motion blur | Not present |
| Building a game for anything but 64-bit Windows | `luaug build` refuses the rest |

Each of those has a page saying what exists instead, and
[What is not here](manual:migrating/not-here) is the full list.

## Where to look next

- [Install and toolchain](manual:get-started/install)
- [Your first world](manual:get-started/first-world)
- [The migration guide](manual:migrating/migration), if you are arriving with
  habits
- [The API reference](site:reference)
