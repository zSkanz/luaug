# 018 — the eight are in `temporary/`, and two of them need you after all

From the drawing agent. Acting on `027`. Sheet: `018-scripted-eight-sheet.png` —
the eight marked `*`, then `Play`, `Add`, `Locked`, `Model`, `UIListLayout` for
comparison.

You said not to review this folder. I am bringing you two things anyway, because
both are about the icons **outside** it.

## What they measure

```
                    bbox        ratio   mid-grey
Stop            768 x 768        1.00     0.01%
Pause           768 x 768        1.00     0.16%
Duplicate       768 x 768        1.00     0.15%
Grid            768 x 768        1.00     0.29%
List            768 x 768        1.00     0.58%
Back            768 x 745        1.03     0.42%
Forward         768 x 745        1.03     0.42%
Up              745 x 768        0.97     0.40%
```

Radius **58**, gaps **85**, keyline filled exactly, PNG. The midtone is edge
antialiasing only — the shapes are drawn hard and fitted, so every grey pixel is
on a boundary.

Arrows are blunt head plus shaft as you specified. **`Forward` measures 42.8%
from `Play`** — the shaft did the work, and the pair you refused to walk into
twice is not close.

## The first thing: `Stop` collides with `UICorner` at **2.0%**

That is the lowest number anything has measured in this project. `MeshPart` /
`Part`, the one you called indefensible, was 10%.

And it is obvious once seen: `UICorner` is a square with **one** corner rounded;
`Stop` is a square with **all four** rounded. At 16 px, three sharp corners
against four soft ones is about two pixels of difference.

`UICorner` is approved and shipping. `Stop` is a placeholder. So this is not a
request to change anything — it is a warning that **`Stop` cannot be drawn as a
plain rounded square by anybody**, script or model, because that shape is taken.
Whatever eventually replaces this placeholder has the same problem, and it is
better known now than after somebody draws it again.

Bing, for what it is worth, kept trying to put a hole in it. It may have been
reaching for the same conclusion badly.

## The second thing: `List` is not the same drawing as `UIListLayout` any more

`ACTIONS.md` still says `List` is "three solid rounded squares stacked", and
says it is the same drawing as `UIListLayout` so the two should share one file.

**That is no longer true.** You changed `UIListLayout` to a rail with three bars
beside it, precisely because three stacked squares are 1:3 by construction and no
keyline has a band for that. `List` kept the old wording.

I drew `List` as the **rail + three bars** so the alias would still hold. It does
not: it measures **28.8% from `UIListLayout`**, because I picked my own rail
thickness and bar spacing rather than copying yours.

Two clean answers and I do not think it is mine to pick:

1. **Alias it.** `List` -> `src/UIListLayout.png`, no file in `temporary/` at
   all. One drawing, two names, which is what `ACTIONS.md` says should happen
   when a pair is one icon.
2. **Keep them separate** and give `List` its own line, in which case the stacked
   squares wording needs replacing for the same 1:3 reason.

If you say (1) I will delete `temporary/List.png` — or rather move it, since I
still cannot delete.

## Everything else

`Pause`, `Duplicate`, `Grid`, `Back`, `Forward`, `Up` read cleanly on both
panels and none of them is near anything. `Duplicate` sits **47.4%** from
`Model`, so the plus-in-the-corner contingency in `ACTIONS.md` is not needed.

## Next

The six object icons from Bing — `Delete`, `Save`, `Open`, `New`, `Rename`,
`Refresh` — starting now, into `actions/` as real deliveries, with `Refresh`
measured against `RunService` and `HotReloadService` the moment it lands.
