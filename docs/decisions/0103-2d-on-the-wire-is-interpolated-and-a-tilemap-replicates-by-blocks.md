# 0103 — 2D on the wire is interpolated, and a tilemap replicates by blocks

- Status: accepted
- Date: 2026-09-25
- Decided by: the owner's mandate of 2026-09-24, whose M4 lists both:
  replicated sprites interpolated between snapshots as a `BasePart`'s `CFrame`
  is, and a tilemap's live edits travelling so that a level built or broken
  during play is the same on every machine. The shapes below are the agent's.
- Supersedes: decision 6 of [0088](0088-2d-on-the-wire.md) ("`Tilemap2D` stays
  off the wire").
- Relates to: [0076](0076-replicas-predict-their-own-and-draw-the-rest-between-snapshots.md)
  (interpolation), [0099](0099-teams-and-network-ownership.md) (owned parts),
  [0100](0100-the-wire-protocol-is-published-and-versioned.md) (the published
  protocol)

## Context

A replicated `Part2D` is moved kinematically to wherever each snapshot puts it
(ADR 0088). Snapshots arrive every other tick, so every remote sprite steps at
thirty hertz, while a remote `BasePart` glides, because ADR 0076 draws it a few
ticks in the past between two samples.

A `Tilemap2D` is not on the wire at all. A replica loads the same scene and
keeps its own copy of every tilemap in it. That holds until a game changes a
cell during play, which is what a level editor, a destructible wall, a mining
game or a door that opens does. Then the two machines disagree, and they go on
disagreeing, silently. ADR 0088 said a live edit "would be a message of its
own, which no game has asked for yet". The mandate asks for it.

## Decision

### A remote sprite is drawn between two snapshots

A replica buffers each remote `Part2D`'s `Position` and `Rotation` per snapshot
tick, and draws it `delay` ticks behind the server's clock, between the two
samples around that moment. This is the same buffer, the same delay and the same
rule ADR 0076 gives a `BasePart`:

- past the newest sample, the newest: a sprite the authority stopped talking
  about is not extrapolated;
- **the rotation takes the short way round.** The 2D solver reports an angle
  in (−180°, 180°], so a sprite turning through 180° goes from 179° to −179°.
  A plain lerp would spin it the long way;
- a field that did not arrive in a snapshot keeps the value of the one before,
  because the diff sends only what changed;
- **a part this machine owns (ADR 0099), or its own character, is not
  interpolated**: it is simulated here.

### A tilemap is replicated, and its cells travel by blocks

**`Tilemap2D` becomes a replicated class.** Its properties are ordinary fields
on the State channel: `Position`, `CellSize`, `Tileset`, `TileSize`, `ZIndex`,
`Color`, `Filter`, `Collides` and `Friction`. `CollisionGroup` stays local, as a
`Part2D`'s does: a group is a name each machine registers for itself. Like every
replicated class, a replica clears its own copy from the scene it loaded and
takes the authority's.

**Its cells travel in a message of their own, a block at a time.** A tilemap
already stores its cells in blocks of 16 × 16, so a block is the unit:

- **`TilemapBlocks`** (a new message, Control channel, to the replica): the
  tilemap's network id, then up to 64 blocks; a larger map takes several
  messages, so no message outgrows what every transport carries. Each block is
  its x and y (as `u32`, two's complement, because a block can be left of or
  below the origin), a `u16` cell count that is always 256, and its cells as
  `u16`. The count makes the layout describe itself, and a replica refuses a
  block of any other size. **A block of all zeros means "empty"**, and the
  replica drops it.
- **On spawn**, after the spawn of the same send, the replica is sent every
  block the tilemap holds. Reliable and after the spawn, so it never names a
  tilemap the replica does not have yet.
- **On an edit**, the authority compares each tilemap whose revision moved
  against **one shadow copy of its blocks as they were last sent**, and sends
  every changed, added or emptied block to every replica that already has the
  tilemap. It keeps one shadow per tilemap, not one per peer, so the cost does
  not grow with the number of players.

A whole block, and not the cells that changed, is what travels. A block is the
unit the tilemap already has, a block on the wire is 522 bytes, and it is
idempotent: a block received twice, or out of a replica's own edit, is simply
the truth.

**A replica's own edit to a replicated tilemap is overwritten** by the next
block the authority sends, as a replica's write to any replicated property is.

### Protocol 15

The new message and the new class are protocol 15. The message takes id 13.

## Consequences

- **The 2D mirror on a replica rebuilds a tilemap's colliders from the blocks it
  receives**, because applying one bumps the tilemap's revision, which is what
  the mirror already watches.
- **Interest**: a tilemap has no position of its own, so it is sent with what it
  hangs from, as a `Model` is. A world's tilemaps go to every replica.
- **The mandate's M4 is these two items.** Sending only the changed cells, and a
  delta encoding for large maps, are not decided here. A block is 522 bytes,
  and a map is sent whole once.

### Rejected

- **Identifying a tilemap by its place in the tree instead of replicating it.**
  Both machines load the same scene, so the "same" tilemap could be found by its
  path. But two tilemaps with one name, or one made by a script on only one end,
  would silently match the wrong one. A network id cannot.
- **Sending cells as properties.** A tilemap's bulk is not a property, and the
  State channel's diff would carry thousands of cells in every snapshot that
  touched one.
