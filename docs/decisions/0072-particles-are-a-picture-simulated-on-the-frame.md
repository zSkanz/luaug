# 0072 — Particles are a picture, simulated on the frame

- Status: accepted
- Date: 2026-09-23
- Milestone: F2 (post-v1 phase 2)
- Decided by: the agent, under the owner's standing instruction of 2026-08-26 to
  take the repository's decisions on their behalf.

## Context

A game wants sparks, smoke, fire and water in quantities -- hundreds per effect,
thousands on screen -- and an engine that made each one an instance would put
every one of them into the world hash, the snapshot, the change queue and the
replication budget, for something no game logic ever reads.

The phase plan also recorded F2 as blocked on one RHI change: a read-only depth
attachment, so a pass can test against the scene's depth while sampling it --
which is what a SOFT particle (one that fades where it meets a surface) and a
screen-space projected decal both need. ADR 0037 froze the RHI, and unfreezing
it is a decision this ADR can take or leave.

## Decision

**1. A particle is not an instance.** `ParticleEmitter` is; its particles live in
`render::ParticleSystem` and are simulated on the frame, by the frame's own
length. What IS world state is the emitter: its properties, and a running total
of the particles `Emit` asked for. The renderer spawns the difference since it
last looked, so a burst is hashed, saved and replicated as one number and the
render side never writes back into the world.

**2. Each emitter's generator is seeded from its instance id**, so a headless
run -- whose frame length is fixed at one tick -- produces the same particles in
the same places every time, and a golden image with sparks in it is one image.

**3. One instanced draw, one pipeline.** A particle is an instance of six
vertices generated from the vertex index, blended premultiplied: the alpha
written is the opacity times what is not emission, so an ordinary particle
blends, an emitting one adds, and one halfway is both -- without a second
pipeline or a sort between the kinds. Particles are sorted back to front among
themselves and drawn after every surface, transparent ones included.

**4. Not soft, and the RHI stays frozen.** Particles are depth-tested and never
depth-written, and a particle crossing a surface shows a hard edge there. The
read-only depth attachment stays unmade: soft particles are the one thing F2
gives up for it, and the decal half of the same need is answered differently
(below), so the change would buy one visual refinement for a frozen interface.

**5. Decals multiply, in a pass of their own between the opaque surfaces and
the transparent ones** -- revised on 2026-09-23 when they were built, from the
clustered design first written here. The cluster design needed a decal atlas
bound in the forward shader, and the terrain shader already uses all sixteen
fragment texture slots, so the one surface decals are most wanted on could not
have had them. Instead the forward pass closes after the opaque surfaces on a
frame that has decals, each decal draws its box and reads the depth those
surfaces wrote (a pass with no depth attachment may), and it MULTIPLIES the lit
colour: the surface's light is its albedo times what reaches it, so this is
the albedo multiplied, with the surface's sun and shadow kept exactly. It works
on parts, terrain and blocks alike and needs no RHI change. What it gives up is
brightening -- a white pixel is no change -- and that is stated in `Decal`'s doc.

## Consequences

- A particle behind a transparent surface draws over it: particles and blended
  surfaces are not sorted together. Accepted and stated in the shader.
- A particle cannot be found, collided with or queried. That is the point, and
  `ParticleEmitter`'s doc says so.
- Particle motion is frame-rate-dependent in the small (integration step), which
  is invisible and is why nothing reads it.
- `ParticleEmitter` replicates (protocol 3): its properties and its burst total
  travel, and each machine simulates its own particles.

## Alternatives considered

**Particles as instances.** Rejected for the cost in point 1: every system that
walks instances would walk them, for data nothing reads.

**Unfreezing the RHI for a read-only depth attachment now.** Deferred rather than
refused: it buys soft particles, and it becomes worth it the day a second caller
-- water foam, a screen-space effect -- needs the same thing.
