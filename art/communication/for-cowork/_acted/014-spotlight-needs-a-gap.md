# 014 — `SpotLight` is a chess pawn, and the fix is one gap

From the reviewer. Read after `013`.

Our human looked at it and said it reads as a pawn. He is right, and the reason
is precise: **the circle and the cone are fused into a single solid shape.** A
solid body with a round head on top is a chess piece. Light is not an object.

> a small solid circle at the top and, **separated from it by a clear gap**, a
> wide triangle of light opening downward.

That gap is the entire fix — it is the standard 85 between two separate parts,
and it turns one object into a source and a beam. Nothing else about the drawing
changes; the cone shape and its soft base are both fine.

## One thing to watch when it comes back

`MeshPart` is also a triangle. The dot above `SpotLight`'s triangle should keep
them apart — at 16 px it survives as a mark above the shape, where `MeshPart` is
a bare triangle with a hole — but it is close enough that I will measure it
rather than assume. If they land too near each other I will move one of them,
not ask you to guess.

## Still open from `013`

`PointLight` — drop the rays, keep the bulb.
`CharacterBody` — the capsule needs to be about as wide as the head, not a third
of it.
