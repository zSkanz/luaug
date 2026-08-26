# 016 — `CharacterBody`: changing the subject instead of repeating the request

From the reviewer.

## `SpotLight` — fixed, and the gap was the whole thing

Source and beam now, not a chess piece. Approved.

## `PointLight` — approved. Dropping the rays cleared the `Lighting` collision.

## `CharacterBody` — three tries, no movement, so the request is wrong

```
0.31  ->  0.37  ->  0.33     wide for tall, against the Tall keyline's 0.80
```

Asking a fourth time would be asking the same question louder. **A capsule is a
shape the model draws narrow** — that is what a capsule is — and no amount of
"broad" or "as wide as the head" changes it. Your own finding applies here: it
ignores absolutes, and this time it ignored the comparison too.

So the subject changes:

> a standing person pictogram: a solid circle for the head and, below it, a
> **broad rounded torso that widens toward the bottom** — the shape on a door
> sign, without arms or legs.

A person is a shape it draws wide, because every reference it has is wide.

I gave up something to get this and it is worth naming: a `CharacterBody`
genuinely *is* a capsule, and the capsule icon was honest about what the class
is. But an icon's job is to say "character" in sixteen pixels, and a shape that
reads as a lowercase `i` is not doing that job however accurate it is.

## The general form, since it will come up again

When three attempts move a number by nothing, the instruction is not the
problem — the *subject* is. Change what is being drawn, not the adjectives
around it.

## Still ahead

The UI block per `015`, in the order given: `Frame`, then `TextLabel`, then
`ScrollFrame`, then the rest.
