# 023 — `Rotate` centres, and `Unlocked` has to reuse `Locked`'s body

From the reviewer. Both of these came from our human looking at the two files
side by side.

## `Rotate` — the arc goes all the way round, and the square centres

My subject line put the arc over the top-right corner, which leaves half the
icon empty and the whole thing lopsided.

> a **solid square in the centre** with a circular arrow running **all the way
> around it**, arrowhead at one end and a clear gap between the arrow and the
> square.

That is the classic rotate gizmo and it is balanced. It still does not collide
with `RunService`: that is a **hollow ring**, this is a ring with a **solid body
inside it**, and a filled centre against an empty one is the strongest
difference two round icons can have.

## `Unlocked` — the body must be `Locked`'s body, unchanged

This is the more useful of the two, and it is a property of the **pair** rather
than of either icon.

Your `Unlocked` reads fine on its own. Beside `Locked` it is a different
padlock: the body is narrower, taller, shifted right, and its corners are less
rounded. Each of those is defensible in isolation. Together they stop the two
being one object in two states.

> **the body of `Locked`, unchanged in every dimension and every corner
> radius**, with only the shackle moved: lifted clear and swung open to the
> left. The keyhole stays where it is.

The rule, now in `ACTIONS.md`: **in a toggle pair, only the part that carries
the state may change.** A person reads these by spotting what moved, and that
only works if nothing else did.

It is the second thing today that was invisible in a single icon and obvious in
a pair — the first being `Locked`/`Unlocked` measuring 9.9% apart. Same two
icons, caught twice, once by measuring and once by looking, and neither method
would have found what the other did. The measurement said *these are too
similar*; the eye said *these are too different in the wrong places*.

## Nothing else changes

`Locked` is untouched and is the reference. If it is easier to work from, take
its body as given and move only the shackle.
