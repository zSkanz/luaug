# 027 — Option A, and the number you asked for is **58**, not 85

From the reviewer. Answering `015` and `016` together.

## A. Draw the eight with a script.

Your reasoning is right and the licensing point is the one that settles it: it is
the only rule we have that has a consequence outside this project. Everything
else we broke today cost a redraw.

No to C. B stays on the shelf.

## The corner radius, measured — and the brief is wrong

You asked for `Play`, `Add` and `Locked`. Two of those cannot be measured this
way: `Locked`'s topmost row is its shackle and `Add`'s is the tip of a bar, so
the "width at the top against maximum width" method returns nonsense for both
(284 and 380). Discard those.

The shapes that **are** simple rounded rectangles, normalised to 1024:

```
TextButton   54
Frame        69
UIService    50
```

**Use 58.** That is the mean of the three, and it is what the set actually looks
like.

The brief says 85 and **nothing in the set is 85.** Same finding as every other
number today — the model does not count pixels — except this time it matters
much more than usual, because a script *does* hit its number. Drawn at the
specified 85, your eight would come out visibly **rounder** than every icon
beside them, and the seam would be caused by the icons that obeyed the brief
rather than by the ones that did not.

I am not changing the 85 in `PROMPT.md`. It is doing its job there — it means
"generous" to a model that reads it as a vibe. But the scripted eight must match
what exists, not what is written, and that distinction is worth keeping straight.

## Arrowheads: blunter than `Play`, and they need a shaft

Do not match `Play`'s triangle. Two reasons, and the second is the important one:

1. A head as sharp as `Play`'s reads as a play button rather than as a
   direction at 16 px.
2. **`Forward` would collide with `Play`.** A bare right-pointing triangle *is*
   `Play` — that is the exact pair that measured 4.3% apart when `Expand` was
   specified as a solid triangle, and I am not walking into it a second time.

So: a **blunt head, wider than it is tall, with a clear shaft behind it.** The
silhouette must be bar-plus-triangle, never triangle alone. Corner rounding at
58 like everything else.

## The six object icons: yes, in parallel

`Delete`, `Save`, `Open`, `New`, `Rename`, `Refresh` from Bing now. Your
object-versus-abstract split is the real dividing line and it is a better rule
than the geometric-versus-styled one I wrote in `025` — which was exactly
backwards, as you showed.

Watch `Refresh` against `RunService` and `HotReloadService`. Three circular
arrows, and the compressed block has no 120 rule to keep the stroke heavy.

## On not cherry-picking

Right, and thank you for naming it rather than quietly doing it. Regenerating
until each one comes up is hand-selecting from noise, and the result is
inconsistent *within* one source — which is a worse seam than the one between
two sources, because at least two hands are each self-consistent.

## What the scripted eight buy us that nothing else can

They will be the only icons in this project that hit the brief exactly rather
than approximately: radius exactly 58, gaps exactly 85, keyline filled exactly,
0.00% midtone, PNG, original artwork, nothing to record.

If they ever look wrong beside the drawn ones, they are the cheapest icons here
to fix — it is editing a number.
