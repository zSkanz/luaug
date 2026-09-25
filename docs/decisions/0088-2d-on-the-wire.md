# 0088 — 2D on the wire (protocol 11)

- Status: accepted
- Date: 2026-09-24
- Decided by: the agent, under the owner's mandate of 2026-09-23 (phase 3
  opened; phase 4, multiplayer, already open)
- Amends: ADR 0069 (the wire schema), `docs/briefs/p3-2d-kickoff.md` decision 7

## Context

The 2D layer landed for one machine first, with `Part2D` and `Tilemap2D`
excluded from protocol 10 by name. A 2D game played together needs the other
players' sprites where the authority has them, facing the way they face and
showing the frame of their walk cycle they are on.

## Decision

1. **`Part2D` replicates, on protocol 11.** Its eighteen fields are read from
   its component, which the 2D physics mirror writes quietly, as a
   `BasePart`'s `CFrame` is read from its component (ADR 0069). The fields are
   its placement (`Position`, `Rotation`, `Size`), its motion (`Velocity`,
   `AngularVelocity`), its body (`Anchored`, `CanCollide`, `Sensor`, `Shape`)
   and its picture (`Color`, `Transparency`, `ZIndex`, `FlipX`, `FlipY`,
   `Image`, `ImageRectOffset`, `ImageRectSize`, `Filter`).
2. **A `Vector2` travels as a `Vector3` with a zero z.** The codec has a
   twelve-byte encoding already. A `Vector2` one would save four bytes a field
   and add an encoding that every reader, writer and wire test must learn. At a
   position and a sheet offset per sprite per snapshot, that is not the saving
   this protocol needs first. A later version can add the encoding beside this
   one.
3. **Sprite animation state is replicated state.** `FlipX` and
   `ImageRectOffset` are how a 2D character walks and turns. A replica without
   them would show every other player sliding in a pose.
4. **A replica drives a replicated `Part2D` kinematically**, as it drives a
   `BasePart`. The 2D mirror makes the body kinematic and moves it where each
   snapshot puts it. It never writes the solver's answer back, so what the
   wire said is what the scene holds. The replica's own contacts still see the
   body where the authority has it.
5. **Interest measures a sprite on the plane.** A `Part2D` is positioned at
   `(x, y, 0)` for the streaming radius, and a 2D `Player.Character` is a
   focus, as a 3D one is.
6. *(Superseded by [0103](0103-2d-on-the-wire-is-interpolated-and-a-tilemap-replicates-by-blocks.md):
   a tilemap is replicated, and its cells travel by blocks.)*
   **`Tilemap2D` stays off the wire**, for `Terrain`'s reason (ADR 0069,
   decision 7). Its bulk is its painted cells, which are not a property. A
   level arrives with the world: from the scene, or from a script that builds
   it the same way on both ends, as `examples/20-platformer` does. A live
   tile edit would be a message of its own, and no game has asked for one.

## Consequences

- Protocol 10 and 11 peers refuse each other at the handshake, as every bump
  does.
- A replica's sprites are exactly where the last snapshot put them. They are
  not interpolated between snapshots as a `BasePart`'s `CFrame` is. That is the
  next thing to add if a 2D game at a low snapshot rate shows it.
