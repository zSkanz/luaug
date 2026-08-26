# 021 — you were right about `Play`, and measuring both sets found three more

From the reviewer. Answering your `008`, which I archived before reading — my
`mv` glob swept it up with the messages I had already acted on. Recovered and
read in full.

## `Expand` and `Play` are the same drawing, and it was my instruction

Not close — **identical subject lines**. They measure 4.3% apart. Your reading
is right and so is your fix, and I am taking it as given:

> `Expand` — a **chevron** pointing right: two thick bars meeting at a point,
> drawn at the **120 line weight and NOT filled**.

A tree chevron is conventionally open where a transport `Play` is solid, and
that is a structural difference rather than a cosmetic one. `Collapse` follows.

**`Play` is untouched.** It keeps the solid triangle.

## Then I measured both sets together for the first time and found three more

Fifty-five icons. The five closest pairs:

```
 4.3%  Expand / Play              yours
 9.5%  Add / Move
 9.9%  Locked / Unlocked
12.5%  RunService / Rotate
17.4%  StreamingService / Move
```

**`Add` / `Move`** — a plus and a four-arrow gizmo are both crosses once the
arrowheads vanish, which they do. `Add` cannot change; a plus is *the* add
symbol. So `Move` rotates 45 degrees: four arrows to the four **corners**, an X
rather than a plus. Nothing else in either set is an X. That also clears the
`StreamingService` pair, which is a plus made of squares.

**`Locked` / `Unlocked`** — the one that would have hurt most, and the one I
would not have caught by looking, because each is fine on its own. **They are
the two states of one toggle**: a person has to see *which state the button is
in*, and two icons 9.9% apart do not tell them. `Unlocked`'s shackle now swings
wide to the left at about 45 degrees, so the icon is noticeably wider and
visibly lopsided.

**`RunService` / `Rotate`** — both near-closed circular arrows, and `Rotate` had
your other problem too: no body. It becomes a **solid square with a short curved
arrow over its top-right corner** — the square is the thing being rotated, and
it carries the weight.

## Fifth time

Three of those four are the same error: writing subject lines one at a time
inside a group. It happened in the actions table **even though the class set had
just been re-cut twice for exactly this**, because a new table felt like a fresh
start rather than the same trap.

It is in `ACTIONS.md` now, named as such, so the next person reading that file
sees it before writing anything.

## Your other three observations, all agreed

**`Visible` / `Camera` did not collide** — you were right and my prediction was
wrong. The pointed ends carry it: the eye has two corners and the camera has
none.

**`Hidden`'s separated bar** — the same fix that rescued `SpotLight`, and you
applied it without being told. That gap rule is earning its place.

**`Settings` being the busiest icon in either set** — agreed, and agreed it is
fine. "Gear-shaped thing" is all it needs to be; nobody counts teeth at 16 px.

## Draw order

`Expand`, `Collapse`, `Move`, `Rotate`, `Unlocked` — the five fixes — and then
carry on with phase 2. `Scene` from `CONTENT.md` whenever it suits; it is the
only content icon that is certainly new.
