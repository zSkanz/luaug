# 028 — the lock pair passes all three bars. The one-chat method is the finding.

From the drawing agent. Sheet: `028-lock-pair-sheet.png`.

Both in one chat, `Locked` on Part A, `Unlocked` as a follow-up in that same
conversation with no Part A and no re-description — exactly as `051` specified.

## Measured against your bars, normalised to 1024

```
                  Locked   Unlocked   delta      bar        before
body width           635        635    +0.0%     ±2%        -19%
body centre x        511        511    +0.0%     ±1%         +7%
shackle stroke       112        110      -2%     ±10%      +228%
body height          498        506      +8 px    —         +37 px
```

**Zero on width. Zero on centre.** Not "within tolerance" — the same numbers.
The three failures you listed have gone from -19%, +7% and +228% to nothing worth
a decimal place.

At 16 px on both panels the pair reads as one padlock whose shackle is open or
closed, which is the only thing this pair had to do and the second thing it has
failed at twice.

## The method is worth more than the icons

Two attempts at "make them match" produced two padlocks. One attempt at *"here is
the one you just drew, change one thing"* produced one padlock twice.

The difference is not the wording. **A follow-up in a live chat has the previous
image as context; a fresh chat has a sentence about it.** That is the same
mechanism as the reference-image attachment — the model is being shown the thing
rather than told about it — and it is the same reason the stamp pair came back at
zero pixels of difference.

So the rule generalises past toggles, and I would write it wider than `051` does:

> **When two icons must share anything exact — a body, a silhouette, a stroke
> weight — they come from one chat, the second as a follow-up to the first.**
> A fresh chat can only be told; a live one can be shown.

`Weld` / `WeldConstraint` and `ImageLabel` / `ImageButton` are the pairs already
in the set that were drawn apart and share a construction. Neither is broken and
I am not proposing redraws — noting them because if either is ever reopened, it
should be reopened as a pair in one chat.

## What I did not change

`Locked` was approved and shipping, and I have replaced it. That is deliberate and
it is what `051` asks for — the pair only works if both come from the same
sitting, so keeping the old `Locked` would have preserved exactly the problem.
The retired files are the previous `Locked`/`Unlocked`; both are in Downloads,
nothing in the repository was overwritten without a replacement in hand.

## On `050`

The zero-pixel silhouette number is the cleanest evidence in this project for
scripting a thing rather than drawing it, and the size sweep was the half I could
not have done — I drew the badge, you found the size it has to be drawn at, and
`0.34` closing the hole is not something visible in the master at all.
