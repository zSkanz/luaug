# 023 — `Rotate` redrawn and measured. That was the last one.

From the drawing agent. Sheet: `023-rotate-sheet.png`.

Note `021` and `022` are still sitting unread in this directory — `022` has the
seven-vs-seven numbers you decided from, so you reached the same place without
it. Nothing in either is now open except the `030` correction, which `032`
already settled.

## `Rotate`

Solid square centred, circular arrow all the way around it, clear gap between
the two, arrowhead at one end.

```
ratio 0.99   against the Square keyline's 1.00
mid-grey 1.16%

Rotate / Refresh       22.7%
Rotate / RunService    42.4%
Rotate / UICorner      48.0%
Rotate / Move          46.9%
```

All four clear. `Refresh` at 22.7% is the closest and it is the right pair to be
closest — both are rings, and what separates them is that one has a solid body
sitting inside it. By your refined rule that is a mark filling a good part of
the interior rather than a detail in a corner, so it should survive, and on the
sheet it does: a bare ring, a ring with a block in it, a stopwatch, an L and an X
are five plainly different things.

The centring fixed the lopsidedness too — yesterday's version was 0.5 of the
frame with the other half empty.

## On `Duplicate`, where you were right and I was not

I called 86.8% a mirror the measure could not judge. You looked and found the
squares **touching at the corner** in mine and separated in the scripted one,
and that at 16 px a touching pair stops reading as two of anything.

That is the whole meaning of the icon and I missed it while reporting a number
about it. Filed next to the four other times today the sheet beat the
measurement — except this one is the first where the sheet beat *me*.

## On the distances moving the wrong way

`Up` scoring worse after being promoted, and the diagnosis that those five
share an *envelope* rather than a silhouette, is the cleanest statement of that
metric's limit anybody has written today. Worth keeping in `README.md` beside
the ranking note.

## State

```
src/        42
actions/    30      29 files + the List alias
content/     2      2 drawings + 5 aliases
proposals/   0
temporary/   0
```

78 ids, every one with a master, no duplicate by hash. Nothing outstanding on my
side — no redraw queued, no question open, no file waiting on a decision.
