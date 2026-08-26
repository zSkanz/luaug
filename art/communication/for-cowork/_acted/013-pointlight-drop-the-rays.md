# 013 — `PointLight` loses its rays, and `CharacterBody` still needs width

From the reviewer. Read after `012`.

## `PointLight` — good bulb, wrong halo

The bulb itself is right: round glass, neck, screw base. But the three rays make
it a radial shape, and `Lighting` is *nothing but* a radial shape. They measure
21.1% apart, and on the sheet both are "a round body with dashes around it".

> a solid light bulb seen from the front: a round glass, a narrow neck, and a
> screw base beneath it. **No rays.**

The bulb silhouette already says light. Dropping the rays costs nothing and the
base gives it an outline the sun does not have.

`SpotLight` is safe as specified — a cone opening downward is neither radial nor
a bulb.

## `CharacterBody` — 0.31 to 0.37, still narrow

It moved a little and it needs to move a lot. At 0.37 wide for tall it is still
a lowercase letter rather than a person. The Tall keyline is **683 wide by 854
high** — the capsule should be nearly twice as wide as it currently is.

If the model will not go that wide from a description, one thing that usually
works: ask for the capsule to be **as wide as the head is**, rather than giving
a ratio. It counts nothing, but it compares two things in the same picture.

## The same mistake, three times, and it is mine

`Workspace` was a cube because `Part` had not been considered yet.
`AudioService` became a mixing desk because `Sound` had already taken the
speaker. Now `PointLight` got rays without my noticing what `Lighting` is.

One error each time: **writing subject lines one at a time inside a domain with
more than one member.** It is in the brief now as its own rule, along with the
fix — before writing any subject line, look at what else in the table shares its
domain and decide the whole group at once.

Domains still ahead where this could bite: the UI rectangles
(`Frame`/`ScreenGui`/`TextLabel`/`TextButton`/`TextInput`/`ImageLabel`/`ImageButton`/`ScrollFrame`)
which is eight icons in one family, and the input trio
(`InputContext`/`InputAction`/`InputBinding`).

I will re-read those two groups as whole sets before you draw them, rather than
sending you a line at a time. Give me a nudge before you start the UI block.
