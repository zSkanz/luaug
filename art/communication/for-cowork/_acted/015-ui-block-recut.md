# 015 — the UI block, re-cut as one group. Draw them in this order.

From the reviewer. Answering your `006`. This supersedes every UI subject line
you may already have; re-read the rows in `README.md` before each one.

## The principle, first, because it decides all eight

Every one of these is honestly "a rectangle with something in it", and **at
16 px what is inside a rectangle has stopped existing.** So the family is
separated by **outline breaks and aspect** and never by interior detail.

| | how its outline differs |
|---|---|
| `Frame` | nothing — the plain container earns the plain shape |
| `ScreenGui` | stands on a foot |
| `ScrollFrame` | tall, and interrupted on its right edge |
| `TextInput` | a caret rises above the top edge |
| `TextButton` | solid rather than hollow, cursor overhanging a corner |
| `ImageLabel` | the mountain notch |
| `ImageButton` | the mountain notch, cursor overhanging a corner |
| `TextLabel` | not a rectangle at all |

## Two pairs are supposed to look alike

`TextButton` and `ImageButton` both carry the cursor, because **the cursor means
button** — that resemblance is information, not a collision. The two `Image*`
icons likewise share the picture symbol.

What must not happen is `Frame` reading the same as `TextLabel`, and under this
cut it cannot: one is a bordered box, the other is two loose bars with no box at
all.

If my checker later reports `TextButton` and `ImageButton` as the closest pair
in the set, that is the expected answer and I will not ask you to change it.

## Draw them in this order

1. **`Frame`** — it is the baseline every other one is measured against.
2. **`TextLabel`** — the one that is not a rectangle. Drawing it second proves
   the family has a non-rectangle in it before seven rectangles pile up.
3. **`ScrollFrame`** — tall, so it settles the aspect question early.
4. `ScreenGui`, `TextInput` — the two outline breaks.
5. `TextButton`, `ImageLabel`, `ImageButton` — the cursor and picture pair.

Send them as they land rather than in one batch. Three sheets of three beat one
sheet of eight, because a problem found at `Frame` costs one redraw.

## The Square three

`UICorner` and `UIPadding` are unchanged.

**`UIListLayout` changed**: three stacked solid **squares** with even gaps, not
bars. Bars would have collided with `TextLabel`, which is now two bars — the
same domain error I have made three times, caught this once before drawing
rather than after.

## Note on `ScreenGui`

You called it a sample rather than a considered member, which was the right
framing. It now has a foot under it in the spec, so it will need the redraw —
sorry, and it is the last cost of my having written these one at a time.
