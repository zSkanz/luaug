# 006 — three collisions and two subject lines that were mine to fix

From the reviewer. Read after `005`. Fourteen icons in `src/` as I write this.

## Approved and untouched

`Camera` · `Folder` · `Lighting` · `MeshPart` · `Model` · `Part` · `RunService` ·
`Script` · `HotReloadService` · `StreamingService`

## Collisions at 16 px

**`DebugService` / `Lighting` — 17.4%.** Both are a central body with limbs
radiating out of it, so at 16 px they are one shape. Mine to fix, and the fix is
proportion rather than metaphor — the beetle is right, its legs are too long:

> a beetle seen from directly above: a **large solid oval body** filling most of
> the height, with three SHORT legs on each side. The body has to dominate.

**`Folder` / `TagService` — 17.4%.** A luggage tag with a pointed end and a
folder with a raised tab are both a wide slab with one corner interrupted.
`Folder` is approved and appears far more often, so `TagService` moves. I am
still deciding what to; I will send it rather than have you guess.

**`Script` / `ScriptService` — 17.7%.** Already in `005`: fan the two sheets
apart so the silhouette is visibly wider than one sheet. Not yet landed.

## `PhysicsService` — my subject line was thin

Delivered exactly as asked and it is a swoosh. The arc alone means nothing; the
ball is the whole idea and I left it optional. Rewritten:

> a solid circle in the upper left **and** a thick curved arc sweeping down and
> away beneath it — a ball with the path it fell along.

## `Workspace`

Still the parallelogram — the globe has not landed yet. No action needed if it
is already in your queue.

Worth telling you because it is a good failure: I updated the keyline in my
checker to Circle when I rewrote the subject line, and my checker exempts the
Circle keyline from the proportion test — so the old slab measured **"ok"**
against a keyline it does not remotely fit. The exemption silenced a real defect
for one run. There is now a guard: a shape on the Circle keyline that is wider
than 1.45 or narrower than 0.7 is flagged whatever else passes. It caught the
slab at 2.54 immediately.

Same shape as the lesson from earlier today, from the other direction: a gate
that reports what is not a defect trains you to ignore it, and a gate that goes
quiet on one that is teaches you nothing at all.

## Nothing needs redrawing for pixel weight

Restating from `005` because it saves you work: `Lighting` and `RunService` are
approved. Do not chase 120. The model does not count pixels and the numbers in
the brief are relationships, not targets.
