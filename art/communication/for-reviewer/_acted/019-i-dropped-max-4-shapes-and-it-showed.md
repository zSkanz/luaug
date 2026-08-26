# 019 — I broke the compressed block myself, and `Delete` proved it

From the drawing agent. A correction to `014`, found by drawing.

## What happened

First Bing `Delete` came back as a good bin **inside an ornamental border** —
scrollwork filling the entire frame, the bin as a small panel in the middle.
Nowhere near the brief.

The cause is mine. Compressing the block from my original 464-character test
down to 330 to make room for your rewritten subject lines, I cut this:

```
before:  Thick strokes, max 4 shapes, readable at 16px.
after:   Thick strokes, nothing hairline, reads at 16px.
```

**I dropped "max 4 shapes."** That is the only thing in the whole block stopping
the model from adding decoration, and I traded it for "nothing hairline" —
which is redundant next to "thick strokes" and does no work at all.

My first Bing test looked good partly because that clause was still in it. So
the `Delete` you approved and the `Delete` that came back with a border were
drawn from different prompts, and I am the reason.

## Revised block, 335 characters

```
Flat UI icon. ONE solid pure white shape (#FFFFFF) on pure black (#000000), only 2 colours: no grey, gradient, shadow, texture. Flat, no 3D, no bevel. Inner detail is a HOLE cut out of the solid, never a thin outline. Thick strokes, MAX 4 SHAPES, nothing else in the frame, no border, no pattern. Centred, fills 80%. NO TEXT. Subject: 
```

Changes against `014`: `max 4 shapes` restored and shouted, `nothing else in the
frame, no border, no pattern` added, `glow` and `nothing hairline` and `reads at
16px` dropped to pay for it, `Flat silhouette` shortened to `Flat`.

`Save` fits at 477 of 480. Three characters. That line genuinely cannot be
revised now, only replaced — same warning as `026`, tighter.

I have **not** put this in `ACTIONS.md` yet. It replaces the block recorded
there, and I would rather you saw the reason first than found two versions.

## Worth keeping regardless of what happens to Bing

The lesson is not about Bing. It is that **when a brief has to be compressed,
the clause that stops the model *adding* things is worth more than any clause
that refines what it draws.** Everything in the block describes the icon; only
that one clause describes what must not be there. I cut the only rule of its
kind and got decoration back immediately.

If that generalises, `PROMPT.md`'s four-shape limit is doing more work than its
placement suggests.

## Status

The retry with the fixed block has been generating for four minutes with no
result — Bing's queue, not an error. Nothing from Bing has been written to
`actions/`; `Delete` there does not exist and the sixteen already in it are
untouched.

The eight scripted placeholders are in `temporary/` and unaffected by any of
this — see `018`, which has two things in it that do need you.
