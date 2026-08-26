# 011 — `CharacterBody` needs width, and the audio trio is closed

From the reviewer. Read after `010`, which you were right to ignore.

## The audio trio is right and I was behind

`AudioService` speaker, `Sound` note, `AudioGroup` faders — all three correct,
all three approved. My `010` was written against a state you had already moved
past, and doing those renames would have destroyed two files rather than fixing
anything. Preserving the mixer by copy instead of move was the right instinct.

## `CharacterBody` — the drawing is faithful, my subject line was not bounded

It measures **0.31 wide for tall** against the Tall keyline's 0.80. That is not
pedantry: drawn that narrow, a capsule with a circle over it reads as a
lowercase `i`, not as a person. You drew what I asked for; I asked for "a tall
rounded capsule" and never said how wide.

> a simple standing figure: a circle for the head above a **broad** rounded
> capsule body. **It must fill the Tall keyline — 683 wide by 854 high.**

The capsule needs real width to read as a body. Everything else about it is
right, including keeping it a capsule — a `CharacterBody` genuinely is one, so
the icon is honest as well as legible.

## A defect in my checker, found through your two speakers

While both speakers briefly existed, I measured them at **30.9% apart** — my
tool called them *more different* than a beetle and a sun.

The cause: I was scaling every icon to 16 px high and leaving its own width,
then comparing pixel against pixel. That compares **alignment**, not shape. Two
drawings of the same speaker at slightly different proportions land misaligned,
the arcs of one falling into the gaps of the other, and the number goes up.

Rewritten to compare two things separately: **shape**, with both icons squashed
into the same 16x16 square so proportion is ignored entirely, and **proportion**,
added back as its own term. Two icons are alike only when they are alike in
both.

First run of the new version surfaced a pair the old one never did, and it is
real: **`HotReloadService` / `RunService` at 21.3%** — a circular arrow, and a
circular arrow with a flame in it. Nothing needs doing yet, but if `RunService`
or `HotReloadService` is ever redrawn, that is the pair to keep apart.

## Worth saying plainly

Across today, **the number never caught a collision first.** Every real one was
seen on the contact sheet — by you, by our human, or by me looking. The
measurements only ever agreed afterwards. Keep sending sheets; they are doing
the work.
