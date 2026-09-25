# 0095 — A light can stand on its own

- Status: accepted
- Date: 2026-09-24
- Relates to: [0013](0013-vector3-native-luau-vector.md) (the -Z look
  convention), [0090](0090-a-material-is-an-asset-a-part-wears-one-and-a-script-clones-one.md)

## Context

`PointLight` and `SpotLight` shone from the `BasePart` or `Attachment` above
them and had no place of their own, which is the reference platform's model:
its documentation says to insert a light "into an Attachment or a BasePart".
A light dropped straight into the Workspace therefore lit nothing, with no
message -- found twice in one day by people using the editor, the second time
in a project where the only light in the scene was one of those.

The owner was shown what the reference does and decided:

> A pointlight e a spotlight não necessariamente precisam de part ou
> attachments; se eu jogar ela no ambiente deve funcionar. Elas devem ter
> posição e rotação.

## Decision

Both lights gain a `CFrame`, the identity by default, read the way `Decal`'s
already is:

- **Nothing holds the light**: `CFrame` is where it is in the world. A spot
  points along its `LookVector` (-Z, ADR 0013).
- **A `BasePart` or `Attachment` holds it**: `CFrame` is relative to the holder
  -- the part, then the attachment's offset, then the light's own -- so the
  identity is exactly where every light was before this, and a light already
  in a part draws the same pixels.

`render::lightAnchorOf` is the one answer: the renderer shines from it, the
editor draws a selected light's rings and cone from it, and the move and rotate
handles work on a light the way they work on an attachment (the drag is
divided back through the holder). A light the editor inserts into a container
that does not hold it -- the Workspace, a folder -- is placed in front of the
camera like a part; one inserted into a part keeps the identity, so it is at
the part.

## Consequences

- A script written for the reference platform never sets a light's `CFrame`,
  so it behaves as it did there; the difference is only that a light nobody
  gave a holder now shines instead of doing nothing.
- The light's own `CFrame` is not interpolated between ticks (a held light
  follows its part, which is). A free light a script moves every tick steps at
  the tick rate; interpolating it is a follow-up for when somebody animates one.
- Lights are not replicated, so the wire protocol does not change.
