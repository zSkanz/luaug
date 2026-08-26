# 017 — `Frame` approved, `TextLabel` reads as an equals sign

From the reviewer.

## `Frame` — approved

Hollow bordered rectangle, exactly the baseline the family needs. Against
`UIService` it holds up well: solid against hollow is a difference that survives
16 px, which is the whole bet of this re-cut.

## `ScreenGui` — the collision is confirmed, and the foot is the fix

`Frame` / `ScreenGui` measure **10.0% apart** — a hard collision, the closest
pair in the set by a wide margin. On the sheet `ScreenGui` is `Frame` with a
small square in the corner that stops existing at 16 px.

Nothing new: this is the redraw already asked for in `015`. Mentioning it only
so the number is on record as agreeing with the plan.

## `TextLabel` — two long bars is an equals sign

It is exactly what I asked for and what I asked for was wrong. Two bars, both
long, stacked — the result reads as `=`, and at **3.14 wide for tall** it is the
most extreme proportion in the set.

> three horizontal bars stacked, the first two long and the third clearly short,
> like the last line of a paragraph. **The block of three must fill the Wide
> keyline, 854 by 683** — shorter bars and a taller stack than instinct
> suggests.

Three bars with a short last line is the universal "text" glyph, and stacking
three of them gives the block enough height to sit at the keyline instead of
being a sliver.

## The proportion rule gained the distinction it was missing

I have been treating "over 40% drift" as a single case and it is two:

**Inherent** — a gamepad is 1.67 because gamepads are; a weld is two blocks side
by side and cannot be square. That proportion belongs to the subject. Leave it.

**Chosen** — an icon assembled from loose parts has whatever proportion the
composition was given. `TextLabel` is bars floating in space; nothing about
"text" demanded 3.14. There the keyline is a real instruction.

The test is one question: *could this subject have been drawn near its keyline?*
If no, leave it alone. If yes, it should have been.

That is why `Weld` at 2.21 stays and `TextLabel` at 3.14 does not.

## Order from here

`TextLabel` again, then `ScrollFrame`, then `ScreenGui` with its foot, then the
rest of `015`.
