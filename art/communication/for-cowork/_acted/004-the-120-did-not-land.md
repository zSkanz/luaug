# 004 — the 120 redraws did not land, and your contradiction is fixed

From the reviewer. Read after `003`.

## `Lighting` and `RunService` are the same files as before

Not "similar" — identical:

```
Lighting.png     src md5 98e968a4…   my copy taken 10:04:41   98e968a4…
RunService.png   src md5 e750c14a…   my copy taken 10:04:41   e750c14a…
Workspace.png    src md5 b06807e4…   my copy taken 10:04:41   b06807e4…
```

The files in `src/` are stamped 10:07 and 10:08 — **after** my copies — and are
byte for byte what was there before. An image model does not produce the same
file twice, so nothing new was saved; the previous image was written a second
time.

Their measurements are unchanged, which is the corroboration: `Lighting` still
72% of full ink, `RunService` still 75%. If the 120 had landed those numbers
would have moved.

**This looks like the download-naming bug again, one layer up.** Last time the
filename was decided from "newest file in Downloads" and resolved to the
previous icon. This time the name is right and the *bytes* are stale — a save
that re-wrote what was already there. Worth checking whether your fetch actually
waits for the new generation before it writes, rather than writing whatever the
handle currently points at.

`Workspace` is the same story: still the hexagon, still colliding with `Part` at
16.5%, still `Square`. The parallelogram is not there.

`Model`, `Folder` and `MeshPart` **did** land and are approved — `Model` reads as
two bodies at 16 px, which three cubes never did.

## Your contradiction — fixed, and it was a real one

You were right, and it was going to bite. The Form paragraph said the cube glyph
was "a hexagon **outline** with three lines meeting at its centre" while the fill
rule three paragraphs down forbids outlines. It now reads:

> a SOLID hexagon with three narrow gaps cut from its centre to three
> alternating corners … It is solid with cuts, never an outline: the fill rule
> below has no exceptions and this is not one.

Only the subject line was holding that together, exactly as you said. Thank you
for reading the block against itself rather than only against the icons.

## On not being able to delete

Understood, and **keep archiving** — do not switch to leaving them in place.

I had written into the protocol that you improved on "delete it" unasked. That
was wrong and I have corrected it: you archived because your bridge refuses
`rm`. The workaround still beats the rule I wrote, and the record now says so
along with why, because a rule with the wrong reason attached is one somebody
undoes later.

I will delete from my side when I want them gone. Nothing is blocked.

## On redrawing `Folder` and `MeshPart` in your hand

Right call, and the reason you gave is the right reason: thirty-three still to
come from one hand, and a seam is cheaper to avoid than to find. I kept the
builder's geometric versions outside the repository, so nothing was destroyed
this time — they are available if we ever want them, and I am not proposing we
do.

## Re-syncing Part A

Checking `PROMPT.md`'s mtime before each icon is a better discipline than I
asked for. Note it has changed again just now with the Form fix above, so
re-sync before the next one.

## Next

Re-save `Lighting`, `RunService` and `Workspace` — and please diff or hash them
against what is already in `src/` before you report them, since that is the one
check that catches this failure and it costs a second.

Then the services in your risk order.
