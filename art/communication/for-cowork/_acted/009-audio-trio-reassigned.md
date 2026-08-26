# 009 — the audio trio swaps places, and one of them is a rename

From the reviewer. Read after `008`.

## `AudioService` as a mixing desk was wrong

Our human looked at it and said it reads strange, and he is right: three bars
with notches say "barcode" rather than "audio". The icon is well drawn — the
brief was wrong.

The mistake was mine and it was structural: I assigned these one at a time.
`Sound` got the speaker first, so `AudioService` had to be something else, and
"something else" got chosen to avoid a collision rather than because it was
right. **A silhouette picked to dodge another icon is how `Workspace` became a
parallelogram too.** Second time today.

## Reassigned as a group, and it costs one draw

**`AudioService`** — keyline Square → **Wide**:

> a solid speaker — a small rectangle joined to a trapezoid that widens to the
> right — with three thick curved arcs radiating from its wide end.

The speaker is the audio symbol, and the service is the audio system. It should
have had it from the start.

**`Sound`** — keyline Square → **Tall**:

> a solid musical note: a filled oval note head, low and to the left, with a
> thick straight stem rising from its right side and a flag at the top.

A note is instantly readable at 16 px and shares its outline with nothing else
in the set. `Sound` is also the one of the three you see most in a tree, so it
benefits most from an unmistakable shape.

**`AudioGroup`** — stays Square:

> three thick vertical bars side by side, each with a square notch cut out at a
> different height, like the faders of a mixing desk.

**That is the icon already sitting in `src/AudioService.png`.** A group of audio
channels *is* a set of faders — the drawing was right and only the name was
wrong. Rename it to `AudioGroup.png` rather than redrawing anything.

So: one rename, and two icons to draw that were on your list anyway.

## The general lesson, now in the brief

An icon chosen to be unlike another icon, rather than to be the thing it names,
tends to end up meaning nothing. It has happened twice — `Workspace`'s
parallelogram and this mixer — and both times the fix was to stop solving one
icon at a time and re-cut the whole group.

When several icons share a domain, **decide the whole set of them together and
give the most recognisable symbol to whichever one earns it.**

## Approved since `008`

`UIService` · `InputService`

`InputService` is one of the best in the set: the controller silhouette carries
it completely, and the d-pad and buttons vanish at 16 px without costing
anything. That is the fill-and-silhouette rule working exactly as intended —
detail as a bonus, never as the load-bearing part.
