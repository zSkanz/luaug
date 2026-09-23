# 0084 — `Ambient` lights enclosed spaces, `OutdoorAmbient` open ones

- Status: accepted
- Date: 2026-09-23
- Decided by: the agent, under the owner's mandate of 2026-09-23 and their
  terrain report ("cave lighting produces regions that are too dark"), on the
  reference platform's model: "we base on theirs, which does not mean copying".
- Amends: [0038](0038-visual-fidelity-is-a-v1-target.md) (the environment and
  the ambient it lights by), and the terrain's openness term from
  [0082](0082-terrain-is-a-grid-of-voxels.md)

## Context

`Lighting.Ambient` was one flat colour added to every surface's diffuse
lobe. On terrain it was multiplied by the vertex's sky visibility, along with
the sky's own irradiance. So inside a cave, where sky visibility is zero, the
ambient was zero too, and whatever no lamp reached was black.

That is backwards for the one term that exists to stand in for bounced light.
The owner's report measured the symptom, the abrupt fall to black inside every
cave, and asked for an ambient that does not vanish there.

The reference platform splits the term in two, and the split is the answer:
- one colour for surfaces that see the open sky;
- one for surfaces that are enclosed.

A world that sets them equal is lit the same inside and out. A world that
wants dark caves darkens only the enclosed one.

## Decision

- **`Lighting.OutdoorAmbient`**, a `Color3`, is the flat light of surfaces that
  see the open sky.
- **`Lighting.Ambient`** is the flat light of surfaces that see none of it:
  a cave, a tunnel, under an overhang.
- **A surface takes a blend of the two by how much sky it sees**, the terrain's
  per-vertex sky visibility: `lerp(Ambient, OutdoorAmbient, sky)`.
- **Both default to the old `Ambient` value**, so a world that sets neither
  changes only inside caves, which now carry that ambient instead of none.
- **The sky's own light**, its irradiance and its reflection, still reaches only
  as much of a surface as sees the sky. A cave is dim, not lit like the plain.
- **Parts, particles and every surface with no sky term are outdoors.** They
  have no way to know otherwise until the engine has a light grid, and outdoors
  is what `Ambient` meant for all of them before.

Every example and test scene that set `Ambient` set it for its outdoor look, so
each now sets `OutdoorAmbient` to the same value as well. `17-cave` sets a
lower `Ambient` of its own, so its chamber stays a cave lit by its crystals.

## Consequences

- **A behaviour change for scripts, stated in the changelog.** A script that
  set `Ambient` to light its parts now lights its enclosed spaces. The parts
  are lit by `OutdoorAmbient`, which starts at the same default, so they change
  only if the script had moved `Ambient` away from it.
- The frame uniforms gain one row, so every capture golden moves once.
- The world hash covers the new property, so every determinism trace moves
  once.
- What a light grid would add later is sky visibility for things other than
  terrain: a part in a cave that knows it is in one. The blend is already the
  shape that information would flow into.
