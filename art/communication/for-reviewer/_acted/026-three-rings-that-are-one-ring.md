# 026 — the three ring weights are in, and they are not three weights

From the drawing agent. Sheet: `026-c1-ring-sheet.png` — `C1` and the three
variants, in colour and in one ink, at 16 / 20 / 24 / 32.

Files: `C1-r15.png` `C1-r20.png` `C1-r25.png`. Reference image attached each
time, each in a **fresh chat** so all three refine `C1` rather than each other.

## The reference image works. It is a step change.

All three are recognisably the same drawing: same crescent, same framing, same
colours, same composition. Words never got us that in seventy-odd icons — every
"redraw with X changed" produced a different drawing. This produced the same one.

That is worth writing into the brief on its own.

## But the model cannot hit a ratio, and the numbers say so

Ring thickness measured across the mark's mid-row, normalised to 256:

```
C1 original      6 px
C1-r15          17 px     asked for 1.5x   ->  got 2.8x
C1-r20          18 px     asked for 2.0x   ->  got 3.0x
C1-r25          20 px     asked for 2.5x   ->  got 3.3x
```

Ink coverage at 16 px in one ink: original **13%**, and the three at **25 / 26 /
27%**.

**They are one thickness drawn three times.** The spread you asked for — a light,
a medium and a heavy to choose between — does not exist in these files. The model
read "thicker" and drew "thicker"; the multiplier was decoration. Same finding as
85 px meaning "generous" rather than 85, arriving in a place where it costs more
because the whole point of the round was to compare three values.

## What that means for the decision

**There is nothing to choose between here, and I do not think there should be.**

The real result is the one the sheet shows plainly: at 6 px the ring is a
hairline that goes to a faint grey halo at 16 in one ink — the thing you correctly
identified as needing fixing — and at ~18 px it is solid, present, and the
satellite reads as sitting *on* it. That improvement is real and all three have
it.

Choosing between 2.8×, 3.0× and 3.3× by eye on this sheet would be choosing noise.

**The ring weight is a number, and it should be set in the SVG.** That is the one
step in this pipeline that *can* hit a ratio, it is the step `C1` was chosen to
make possible, and it costs one attribute. Pick the weight when you author it,
with the crescent's stroke and the satellite's diameter in front of you as real
numbers — not by asking a model for 2.0 and measuring 3.0 afterwards.

If you want a recommendation for that attribute rather than a drawing: **the ring
around 18 of 256** is where these three sit and it is clearly right at 16 px. I
would author it there and adjust against the crescent by eye at 16, not by ratio.

## The gap did land

Your note and mine — that the satellite has to stay a distinct object when both
go black — is satisfied in all three. In one ink at 16 the disc is still a disc
against the ring, which is not true of the original at the same size.

## Not doing anything else

No further generations on `C1` unless you ask. The lockups, the one-colour mark,
the icon sizes, the social card and the hand-authored SVG are all yours and none
of them wants a model.

`024` and `025` are still unread in this directory; `025` is only the note that
`D2` had already landed, and `024` has the eight-candidate sheet.
