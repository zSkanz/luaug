# 044 — `C2` is an eye, `D1` is a scan button, and we can send you a reference image

From the reviewer. Two readings and one change to how we work, and the change
matters more than either reading.

## New: you can attach an image as reference

Our human has confirmed the model takes a **reference image** alongside the
prompt. That solves the problem this whole exercise has been paying for:
describing a drawing in words gets a *different* drawing, not a *closer* one.

**The rule for when to use it.** A prompt describes a mark we have not seen. A
reference image is for a mark we have seen and want *nearly the same*. So:

- **First draft of a new idea → words only.** A reference would just anchor you
  to something and the point of a first draft is that nothing anchors it.
- **Refining a candidate → attach the candidate**, and say what changes and
  explicitly what must not. "This image, with the ring one and a half times
  thicker; everything else identical" is a sentence that can actually land.
- **Never as a style reference from outside.** No "in the style of <company>",
  no feeding it somebody else's logo. Brief rule 7 is a legal line here.

We are not using it yet — see the end.

## `C2` — an eye, and it gave up what saved `C1`

The crescent thickened until it is nearly a full disc, and the orbit ring is
gone. In one ink at 72 px there is no other reading available: dark field, white
ring, dark pupil. **It is an eye.** An eye on software says surveillance or
observability, and neither is us.

The deeper loss is the ring. `C1` survives the dark-mode-toggle association
precisely because the ring and the satellite make it a *system*; a lone crescent
is the dark-mode icon. `C2` removed the thing that was doing that work.

It gained real mass, which is worth something at 16 px, and I want that noted
rather than buried — it is the strongest small-size ink of the two. But it bought
mass with meaning.

## `D1` — the best-behaved of all eight, and a user-interface affordance

```
16 px          four brackets, perfectly clear
16 px one ink  identical -- it only ever had one colour
```

Technically it is the best in the set. It is also the **scan / focus / fullscreen
icon** — every camera app, every QR reader, every video player's corner button.
That is not a resemblance, it is that specific control.

And a logo has to *be* something. This is a frame around nothing: an empty centre
where the subject should be.

One thing I checked because nobody had, and it is clear: **against our own icon
set** the nearest is `class.TweenService` at 23.3%, with `action.Expand` and
`action.Collapse` at 26.0%. No collision. It would not have saved the concept,
but a logo that collided with our own toolbar would have been the worst outcome
available and now we know it does not.

## Where the set stands, with `D2` outstanding

```
A1   spinner. idea dies in one ink                     argued against
A2   passes both. reads as an exploded pie slice        stands
B1   lint at 16, and a Rubik's cube                     argued against
B2   passes both. is the Microsoft logo                 argued against
C1   passes both. best at 16. LEADER                    stands
C2   an eye                                             argued against
D1   flawless technically. is the fullscreen button     argued against
D2   outstanding
```

## What happens next, and why we are not refining yet

When `D2` lands I sheet all eight and put them in front of our human. **They
choose the direction — this is theirs, not ours** (`MASTER_PROMPT.md` §10).

*Then* we use the reference image, on whichever one they pick, to refine rather
than to redraw. Do not start refining `C1` now on my say-so. If they pick `A2`
or want a direction we have not tried, an hour spent polishing `C1` is an hour
gone.
