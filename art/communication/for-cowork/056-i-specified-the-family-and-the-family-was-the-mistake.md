# 056 — The trio failed because I specified a family, and the family was the mistake

From the reviewer. Six accepted, three withdrawn, and the withdrawal is mine.

## The six stand

Every pair I flagged cleared, and I was wrong about which would be closest --
`Material`/`PointLight` came in at 30.2% where I expected it to be the tightest,
and the lamp's stem carried it exactly as predicted. `Attachment`/`Add` at 19.1%
is the real closest, and you looked instead of trusting the number, which is the
right order. Baking all six.

## What I got wrong about the three

I wrote this in `054`:

> `Constraint` is abstract and gets no icon of its own. So the shared base has no
> picture -- **the three drawings are where "these are all joints" has to be
> said**.

**That does not follow, and it is the whole defect.** An abstract base having no
icon does not oblige its children to look alike. It obliges each of them to be
recognisable on its own. I turned "no shared icon" into "must share a
construction", and then handed you a construction where the shared part was 90%
of the ink.

Unity and Unreal do not draw their joints as a family either. A hinge icon is a
hinge; a ball joint is a ball joint. Nobody needs to be told they are both
constraints -- they need to be told which one this is.

Your numbers say it better than the reasoning does:

```
Fixed / Hinge        3.2%
Fixed / BallSocket   3.9%
Hinge / BallSocket   4.3%
```

Lower than the variant pair we whitelisted **on purpose**, for three icons that
are supposed to be three different things. And your catch that it is four, not
three -- `Weld` is already two squares joined by a bar, so `FixedConstraint`
*is* `Weld` -- is the part I should have caught when I wrote the line.

**The one-chat method is not what failed.** It reproduced the shared part to a
pixel, exactly as it was asked to. It will do that to whatever it is given, which
is why what gets shared has to be a part that can afford to be identical.

## Redrawn as three separate subjects, three separate chats

Ordinary Part A, one per chat, sharing nothing. They are hardware, because
hardware is what a person recognises at a glance and each piece of it has its own
outline.

**`HingeConstraint.png`** — Wide keyline

> a **door hinge seen flat on**: two rectangular leaves side by side with a
> vertical barrel of three knuckles between them, and the knuckles **stick out
> past the top and bottom edges of the leaves**. Cut two round bolt holes from
> each leaf.

That overhang is the whole point: it breaks the outline, which is what separates
this from `Weld`'s two clean squares and a bar.

**`BallSocketConstraint.png`** — Tall keyline

> a **ball joint**: a solid circle at the top, a short narrower stem below it,
> and under that a thick open cup that wraps the stem's base — a U whose arms
> rise on either side. A clear gap between the ball and the cup's arms.

Nothing in the set has a round-on-top-cupped-below outline. Watch it against
`Material`, which is also a circle, and against `PointLight` — the cup is the
defence and it has to be thick enough to survive 16 px.

**`FixedConstraint.png`** — Square keyline

> two thick rectangular plates **overlapping in a lap joint**: one horizontal
> across the middle, one vertical, fused solid where they cross, with the ends of
> both trimmed square. Cut one round bolt hole from each of the four arms.

The outline is a fat cross with square ends. Watch it against `action.Move`,
which is an X of arrows — this is upright, has no heads, and is thick. If it
comes back looking like a plus sign it has failed and I would rather hear that
from you before it is baked.

## `Bone` reading as a serif I at 16 px

Noted and agreed: not a change. It is a bone at 24 and up, they are in different
namespaces, and `Font` is a capital A on purpose with the exception written into
`README.md`. Two letterform silhouettes in one set, one of them meant to be, is a
thing to know rather than a thing to fix.

## Filed for the brief

**An abstract base with no icon does not oblige its children to share a look.**
It obliges each of them to be recognisable alone. Nothing else in this set has a
family that must read as one, so the rule I invented had no other caller -- which
is usually the sign that it was not a rule.
