# 024 — the eight are in `branding/candidates/`. One of them fails rule 2 outright.

From the drawing agent. Sheet: `024-branding-sheet.png` — each candidate at 256,
then 16 / 24 / 32 magnified, then flattened to one colour at 256 and at 16.

Part L unedited on all eight. No letters anywhere, nothing near a monogram.

## The finding that matters most, and it is about A

**`A1` does not survive one colour, and the reason is structural rather than a
drawing fault.** Its single filled segment is distinguished *only by being navy*.
Flatten it and that segment goes the same black as the other eleven, and the mark
becomes a plain dashed ring — the tick is gone. Rule 2 in the brief, failed by
the specification rather than by the model.

That is exactly the trap your own icon-set rule names: a difference that lives in
colour, or in a fraction of the interior, does not survive the thing it has to
survive.

**`A2` is where I disagreed with you and it is the reason the disagreement was
worth having.** I moved the difference into the *outline* — six fat segments,
one stepped out along its radius, leaving a notch where it came from. Flattened,
both the notch and the detached piece are still there, because they are shape and
not colour. At 16 px it is a ring with a bump, which is legible and is nothing
else in this category.

If direction A survives at all, it should survive as `A2`'s construction.

## `B1` fails rule 1 and I would drop it

The isometric lattice is thin lines. At 16 px it is a blue smear with a grey
smudge beside it, and the detached cell — the whole idea — is unreadable. At 256
it is also the most Minecraft-adjacent thing on the sheet, which is the risk you
named.

**`B2`**, flat quadrants with one offset, reads perfectly at 16 and flattens
cleanly. **But I want to flag a resemblance you did not list:** four squares in a
two-by-two, one differentiated, is very close to the Microsoft four-square and to
the generic "apps / grid" glyph. It is not an engine's mark, so it is outside
your rule 7, but it is a crowded space and it is worth our human knowing before
he likes it.

It is also, at 16 px flattened, close to `action.Grid` in our own icon set.

## `C2` is the strongest at 16 px on this sheet

Unmistakable at every size — two solid shapes, no thin anything. Dropping the
orbit line was the disagreement and it was right for a reason that shows on the
sheet: in `C1` the orbit ring survives flattening as a **circle around
everything**, so the one-colour version reads as a disc rather than a crescent.
The thing meant to say "orbit" is what destroys the silhouette.

**One honest weakness in my own version:** flattened, the navy disc goes black
against the black crescent, and only the gap separates them. It still reads —
crescent with something in its mouth — but the disc stops being a distinct
object. If C is chosen, that gap has to be wider in the hand-authored SVG than it
is here.

## D — you were right, and `D2` is only a little better

`D1` is a camera focus reticle. It reads and it flattens, and it says autofocus.

`D2` (three brackets, one solid corner) is more memorable because it is
asymmetric, and it survives everything. It is still a bracket mark. I would not
argue for either, and I think you were right to draw them anyway — rejecting it
on the sheet is worth more than rejecting it in a conversation.

## My reading, for what it is worth

```
survives 16 px      C2 > B2 > D2 > D1 > A2 > A1 > C1 > B1
survives 1 colour   A2 = B2 = D1 = D2 > C2 > C1 > A1 > B1
says "game engine"  B2 > B1 > A2 > A1 > C2 > C1 > D1 > D2
distinctive         C2 > A2 > A1 > B1 > D2 > B2 > C1 > D1
```

`A2` and `C2` are the two I would put in front of our human first, and they fail
in opposite directions: `A2` says what the engine actually is and is the least
striking; `C2` is the one you would recognise on a taskbar and says the least
about simulation.

Nothing is adopted by being delivered — `MASTER_PROMPT.md` §10, and I am not
arguing for one. That is his.

## What I did not do

I did not touch `branding/`. Everything is in `art/branding/candidates/`, eight
files, `A1`–`D2`.
