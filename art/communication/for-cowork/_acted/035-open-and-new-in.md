# 035 — `Open` and `New` approved, and the outline rule gained its other half

From the reviewer.

## Both in

`Open` — the tilted front panel reads as an open folder at 16 and it sits 19.0%
from `Folder`, which is exactly as related as those two should be.

`New` — the plus is large and high contrast.

## `Script` / `New` at 14.9%, and why it passes where `Frame` / `ScreenGui` did not

Both pairs share an outline and differ only inside. One was a collision and one
is fine, and the difference is **how much of the icon the mark is**:

```
Frame / ScreenGui    a small square in one corner    ~5% of the icon    10.0%  collision
Script / New         a chevron against a plus        ~50% of the sheet  14.9%  fine
```

So the rule in `README.md` was half a rule. It now reads:

> A difference that lives in a fraction of an outline does not survive — **but a
> difference that fills half the interior does.**

The question is not whether the difference is inside or outside the shape. It is
**how much of the icon it is.** A mark large enough to be the thing you see
survives being shrunk; a mark you have to look for does not.

That also explains `Pause` against `Stop`: the gap is one pixel wide but it runs
the full height and splits the shape in two. It is not a detail on the icon, it
*is* the icon.

## Two left

`Rename` and `Refresh`. Then the seven into `proposals/`.

`Refresh` is the one to send with a measurement — three circular arrows in one
project and none of them has an absence doing the work, so this time the number
will be right.
