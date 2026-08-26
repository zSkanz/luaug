# art/editor-icons/ — the class icons for the editor's Explorer

Forty-two icons, one per class the Explorer can show. They are authored art and
therefore a human decision, which is why they live at the top level beside
`branding/` rather than under `docs/`.

- [`PROMPT.md`](PROMPT.md) — the exact text to send, and how to hand files back.
- [`src/`](src/) — the masters, one per class, named for it.
- [`proposals/`](proposals/) — where anyone who is **not** the drawing agent puts
  an icon.
- [`ACTIONS.md`](ACTIONS.md) and [`actions/`](actions/) — the **second set**: the
  editor's own toolbar and chrome. `src/` is what a scene *contains*;
  `actions/` is what a person *clicks*.

- [`CONTENT.md`](CONTENT.md) and [`content/`](content/) — the **third set**: the
  six kinds the content browser can show, from the `ContentKind` enum in
  `engine/app/include/luaug/app/content_tree.h`. Four of the six point at
  drawings that already exist.

All three sets use the same style block, and the checker compares them
**together**, because all three are on screen at the same time.

## One writer owns `src/`

**Only the drawing agent writes into `src/`.** Anybody else — a second agent, a
one-off geometric draw, a hand edit — delivers into `proposals/` under the same
filename, and the reviewer promotes it or does not.

This is not bureaucracy. Two masters have already been destroyed in this
directory, both times because two writers used one filename: the first when a
download was misnamed and `Workspace` landed as `Folder`, the second when a
geometric redraw of `Folder` and `MeshPart` overwrote the drawing agent's
versions of the same two before anybody had compared them. Neither loss was
recoverable, because `art/` is not yet in git.

A proposal costs nothing and can be compared. An overwrite cannot be compared,
because one of the two things being compared no longer exists.

**Nothing consumes these yet.** The editor is in E1 and has no icon path. What
that path has to be is [specified below](#what-the-engine-still-needs) so the art
is not drawn against a guess.

---

## The pairs that are SUPPOSED to read as collisions

A checker measuring pairwise distance will put these at the top of its list and
argue for a redraw. **They are the specification, not defects**, and any run that
flags them should be reading from here:

| pair | why it must read low |
|---|---|
| `Locked` / `Unlocked` | a **toggle** — one object in two states. They share a body by design; that is what took two rounds to achieve |
| `Script` / `ModuleScript` | a **variant** — one document with two marks. The page is the shared half |
| `overlay.Stamp` / `overlay.StampBase` | the same silhouette by construction, to **zero pixels**. Measuring them against each other is measuring a hole |

> **A toggle and a variant are the only two cases where a low pair distance is
> the goal.** Everything else in the set has to be different; these have to be
> the same.

Left un-whitelisted, the best results this set has produced would sit at the top
of its worst-collisions list, arguing to undo themselves.

## `<>` in `ModuleScript`, and why it is not the mark the branding rejected

Recorded because it is the same glyph and somebody will otherwise read it as an
oversight. `art/branding/README.md` spends a section on why `</>` could not be
the engine's mark: it says *web*, and it is on every developer tool there is.

Different job. The logo had to carry the whole engine's identity **alone**, on a
taskbar, with nothing beside it. This is one row in an Explorer, inside a page
outline, sitting directly under the same page with three text bars in it. The
question is not "what is this project" but "is this page code or prose", and `<>`
is the convention for exactly that.

The rule that separates them: **a mark that must identify needs to be unlike
everything; a mark that must distinguish only needs to be unlike its neighbour.**

## A shared body is a FAMILY, and the ranking cannot tell one from a collision

`ModuleScript` arriving put four page-shaped icons in the set, and the pairwise
ranking flagged all of them:

```
ModuleScript / Script    4.1%
New / Script             6.4%
New / ModuleScript       7.0%
Other / Script          11.3%
```

Every one is under the 12% that has meant "look at this" all day. Sheeted at
16 / 20 / 24 / 32 on both panels, **none of them is confusable**: three bars, two
angle brackets, a plus, and a blank page.

What the number is measuring is the page they share — and the page is doing real
work. It says *these are all files* before anything inside it is read, which is
the same job the badge's circular outline does from the other direction.

> **A deliberate family resemblance and a collision look identical to a measure
> of overlap.** Only the sheet separates them, and the question it answers is not
> "how much do these share" but "can I tell them apart".

This is the fifth time today a number has said LOOK and the looking has said
fine. The ranking earns its place by being cheap and by never being trusted.

## When two icons must match, they come from ONE chat

Proven twice in one day, and it is the most useful rule this set produced.

`Locked` and `Unlocked` were specified as sharing a body and drawn in two
sittings. Twice. The result both times was two padlocks: body width off by 19%,
body centre off by 7% of the frame, shackle stroke off by 228%. Redrawn in a
single conversation — `Locked` on Part A, then *"the same padlock open; the body
is identical to the image you just drew; change only the shackle"* — the same
three numbers came back at **0.0%, 0.0% and 2%**.

The stamp badge is the same finding from the other end: base first, mark as a
follow-up, and the outer silhouettes differ by **zero pixels of 1,048,576**.

**The mechanism, and it is not the wording.** A follow-up in a live chat has the
previous image as context. A fresh chat has a sentence describing it. It is the
same reason an attached reference image works — the model is being *shown* the
thing rather than *told about* it.

> **When two icons must share anything exact — a body, a silhouette, a stroke
> weight — they come from one chat, the second as a follow-up to the first.**

Two pairs already in this set were drawn apart and share a construction:
**`Weld` / `WeldConstraint`** and **`ImageLabel` / `ImageButton`**. Neither is
broken and neither needs redrawing. They are named here so that if either is
ever reopened, it is reopened **as a pair, in one chat** — reopening one alone
would rebuild exactly the defect this rule exists to prevent.

And the corollary that caught us out: redrawing one half of a pair means
**replacing the approved half too**, even when it was shipping and fine. Keeping
it preserves the problem.

## What "the way Unity and Unreal do it" actually means

The three engines worth copying do not agree, and the disagreement is
instructive:

| | Format | Themes | Lookup |
|---|---|---|---|
| **Unity** | PNG, with `d_` dark variants and `@2x` | two sets, drawn separately | by string name |
| **Unreal 5** | **SVG** (`FSlateVectorImageBrush`) | one asset, recoloured by the style set | by style key |
| **Godot** | **SVG** | one asset, recoloured by a mapping table at import | by name |

Two of the three moved to vector, for two reasons: a vector icon is sharp at any
DPI, and a single-colour vector recolours per theme instead of being drawn
twice. Unity's `d_` prefix is the older answer and it is the one that costs an
artist double.

**We are on raster, and it is a decision rather than an omission.** Turning these
masters into vector needs an autotracer, and the obvious one — potrace — is GPL,
which R6 does not admit. Finding a permissively licensed tracer, or accepting a
copyleft tool in the art pipeline but not the engine, is an ADR and not a
default.

What we take from Unreal and Godot anyway is the part that matters more than the
format: **one monochrome master, tinted at draw time.** No `d_` set, no second
drawing, no chance of the two themes drifting apart. That is why the prompt asks
for a white silhouette on black and nothing else — the master is a *mask*, and a
mask is a shape plus a colour you have not chosen yet.

## The keyline grid

The real quality lever, and the thing that separates a set from a folder.

Every icon is drawn on a 1024 canvas with 85 px of margin, leaving a live area
of 854. Inside that, the icon's dominant shape fills **exactly one** of four
keylines:

| Keyline | Size | For |
|---|---|---|
| Square | 768 × 768 | boxy subjects |
| Circle | Ø 854 | round subjects |
| Tall | 683 × 854 | vertical subjects |
| Wide | 854 × 683 | horizontal subjects |

Stroke weight, corner radius and the gap between two separate parts are all
**85 px, everywhere, in every icon** — with one exception that was found by
measurement rather than argued: **a cut scored inside a single solid shape is
about 35 px, not 85.**

**And an icon that is a LINE rather than a body is drawn at 120, not 85.** A
circular arrow, an ease curve, the rays of a sun: at 85 those strokes are 1.3 px
at 16 and antialias to grey along their whole length. Measured as the share of
ink surviving at full intensity, `Lighting` lands at **72%** and `RunService` at
**75%** where `Folder` is 96% and `Camera` 91% — they read, but they read faded
in a column beside solids. 120 costs nothing and closes most of that gap.

That measurement replaced a worse one. The first attempt counted how much of the
box was lit, and it was wrong in both directions: `MeshPart` covers 62% of its
box and is one of the crispest icons here, while `Script` covers 94% and is not.
What decides legibility is not how much of the box is used, it is **how much ink
survives at full intensity** — a sub-pixel stroke greys along its whole length,
a solid body does not.

The three lines that make a hexagon read as a cube are such a cut. At 85 they
eat the shape and a cube becomes three petals at 16 px. The brief originally
said 85 for every gap; the approved `Part` measures **35.1**, and `Model`, drawn
by a different hand on a different day, measures 23 to 46. Two hands broke the
same rule in the same place, which is how a rule is discovered to be wrong
rather than a drawing to be.

**And every icon is filled, never outlined.** Inner detail is a hole cut out of
the solid shape, not a thin line drawn on it. This is the rule the first
delivered icon exposed as missing from the brief: an 85 px stroke is 1.3 px at
16 px and greys out, while a solid silhouette stays crisp — the contact sheet
below shows the difference plainly.

Why bother: a square that fills its box and a circle that fills its box look the
same *size* to the eye even though the circle covers less area, while a square
and a circle both drawn "about 80% of the canvas" do not. Every published icon
system — Material, Fluent, Carbon — is built on keylines for exactly this, and
it is the difference between forty-two icons and one family.

The keyline for each icon is in the tables below. It is not a suggestion: an
icon that under-fills its keyline reads as shrunken next to its neighbours, and
no amount of trimming afterwards fixes a proportion.

## The pixel numbers are aspirations, not measurements

An image model does not count pixels. This was tested rather than assumed: the
brief asked for `Lighting`'s rays at 120, the drawing agent passed that through,
and the delivered rays measure **68** — where the previous version, drawn when
the brief said 85, measured **71**. The instruction moved the number by three in
the wrong direction.

The same holds everywhere the numbers have been checked. `Part`'s internal cut
is 35.1 and `Model`'s are 23 to 46, from one brief on one day. Keyline fills come
back anywhere between 89% and 112%.

So the constants are worth stating — they set a *relationship*, and the
relationship survives even when the absolute does not: a cut is much thinner
than a stroke, a line icon is heavier than a body's stroke, parts are separated
by a visible gap. What they cannot do is arrive on the canvas. **What actually
holds the set together is the review loop**, and every rule here that survived
did so because something measured or looked wrong afterwards.

Write the numbers. Do not expect them. Check.

### One note on how `Workspace` got here

It was a cube on a bar, which collided with `Part`. It became a parallelogram —
a ground plane — which was unique and meant nothing: at any size it is a white
slab, and a bare quadrilateral is not a symbol of anything. The second attempt
optimised for a silhouette no one else had and optimised the meaning away.

An icon has to survive two tests and they pull in different directions: **be
unlike every other outline in the set, and be recognisably the thing it names.**
Passing one of them is not progress.

### Every number here ranks; none of them decides

Both measures in the checker were promoted to verdicts and both had to be
demoted, for the same reason and within a few icons of each other.

**Ink at 16 px** was made a threshold, then flagged `Lighting` — which reads
perfectly — because a sun is almost all diagonal edge and diagonals antialias
exactly like a stroke that is too thin.

**Distance between two icons at 16 px** was set at 16%, let `Part` / `Workspace`
through at 16.5%, was raised to 20%, and then failed three pairs that are
plainly distinguishable on the sheet.

Both are good at *ordering* — the riskiest pair really is at the top of the list
— and bad at deciding any single case. So the checker now prints the three
closest pairs and the three lightest icons on every run, unranked as pass or
fail, and only a distance under 13% is called a collision outright, because the
one indefensible case measured 10%.

**The sheet decides.** The numbers say where to look.

### Proportion: the one measure that still decides something

Scaling can fix how big a shape is. It can never fix how wide it is for how
tall, so proportion is the measure that survived being demoted.

| drift from the keyline | |
|---|---|
| under 15% | invisible |
| 15% to 40% | a note |
| over 40% | ask the two questions below |

**Inherent against chosen.** A gamepad is 1.67 wide because gamepads are; a
`Weld` is two blocks side by side and cannot be square. Those proportions belong
to the subject and are left alone. An icon assembled from loose parts — bars,
dots, a stack — has whatever proportion the composition was given, and there the
keyline is a real instruction.

So, before acting on the number, **two** questions:

1. *Could this subject have been drawn near its keyline?*
2. *Would it still say what it says?*

`TextLabel` answers **yes** to the first and **no** to the second. Three short
bars in a near-square block is the hamburger-menu glyph; what makes three bars
read as a paragraph is being wide with a ragged last line. Its 1.99 carries
meaning, which makes it inherent after all — and it was approved there, having
been rejected at 3.14 where the width carried nothing.

The first question alone would have moved it from an equals sign to a menu
button, which is not progress.

**This section exists because the rule spent an afternoon living only inside
messages to the drawing agent.** It was used a dozen times, refined twice, and
was nowhere in the brief — which is the exact failure `art/communication`'s own
protocol warns about, committed on the rule that was doing the most work.

### Breaking the outline is not free

Two icons in the UI family were given outline breaks to keep them apart from
`Frame`, and both breaks produced a different object instead.

`TextInput` got a caret rising above its top edge. Drawn solid enough to survive
16 px, a stem on top of a box is **a sign on a post**. A text caret lives inside
the field, on the baseline; moved outside, it stops being a caret at all.

`ScrollFrame` got a scrollbar protruding past its right edge. A rounded bar
sticking out of the side of a tall rounded rectangle is **a phone's power
button**, and the icon read as a tablet.

The lesson is not "avoid outline breaks" — `ScreenGui`'s foot and `TextButton`'s
cursor both work. It is that **a break has to be made of something the subject
actually has.** A monitor has a stand; a button has a pointer on it. A text
field does not have a mast, and a scrolling panel does not have a side button.
Where the subject has nothing that sticks out, use aspect or fill instead:
`ScrollFrame` is the only tall one in the family, and that was always enough.

### The lowest number in the project, and what it was made of

`UICorner` was "a square with one corner rounded". `Stop` is a square with all
four rounded. They measured **2.0% apart** — lower than `MeshPart`/`Part` at 10%,
which was called indefensible at the time.

Nothing was drawn badly. Both icons are exactly their subject lines. The whole
distinction was **one corner out of four**, which is about two pixels at 16 px,
and it was written down months of icons before the thing it would collide with
existed.

The general form is worth having, and it has a second half that took longer to
find: **a difference that lives in a fraction of an outline does not survive —
but a difference that fills half the interior does.**

`Frame` and `ScreenGui` collided at 10% because the distinction was a small
square in one corner, about 5% of the icon. `Script` and `New` measure 14.9%
apart and are fine, because one carries a chevron and the other a plus and each
mark fills half the sheet. Both pairs share an outline; only one of them shares
its *meaning*.

So the question is not "is the difference inside or outside" but **how much of
the icon is it**. A mark large enough to be the thing you see survives being
shrunk; a mark you have to look for does not. A whole edge, an added limb, a changed aspect, a
filled body against a hollow one — those survive. A corner does not, a caret
does not, a small square in a corner does not. Three of the four hardest
collisions in this project were of exactly that shape.

### An icon needs a body somewhere

`Lighting` measures light and reads perfectly, because a sun is a solid disc
with rays around it — the disc carries the weight and the rays are decoration.
`AnimationPlayer` was specified as a line with three small diamonds on it, and
at 16 px it is a pale grey dash: **the palest thing in the set on either
panel.** Nothing in it is solid.

The 120 line weight helps a stroke; it cannot rescue an icon that is *only*
strokes. So the rule is not about weight, it is about composition: **something
in the icon has to be a body.** A circular arrow gets away with it because a
closed ring encloses an area the eye reads as a shape; a straight line encloses
nothing.

Found by the drawing agent on its own contact sheet, not by any measurement of
mine — my ink number had `AnimationPlayer` outside the lightest three, because
it is short and dense per pixel while being nothing at all as a picture.

### One shape the keyline system cannot hold

`UIListLayout` was first specified as three stacked squares. Three squares with
gaps between them are **three times taller than wide by construction**, and the
arithmetic says no stack of squares can reach even the Tall keyline's 0.80 — the
four bands here, like Material's, simply have no place for a 1:3 shape.

That is not a spec mismatch to be waved through. At 16 px tall, a 0.32 icon is
**five pixels wide**: a vertical sliver beside everything else in the column.

The answer was to change the drawing rather than the band. A left rail with
three short bars beside it says "items arranged along an axis" more precisely
than a stack of squares did, separates cleanly from `TextLabel`, and fits.

**When a subject cannot reach any keyline, the subject is usually wrong.** The
bands are not arbitrary: they are the proportions that sit together in a column
without one looking like a mistake.

### Choosing a second source: check the prompt field, not the pictures

The drawing agent tested Bing Image Creator when the ChatGPT quota ran out, and
it fails on something nobody thinks to look at: **its prompt field caps at 480
characters, and the style block plus one subject line is 3668.**

The block cannot be sent unedited, and unedited is the rule this file opens
with. Compressed to an eighth it lost the keyline grid, the four keylines, the
85 / 35 / 120 weights and the four-shape limit — and the icon that came back
filled **74%** of its keyline, which is the shrunken-beside-the-others failure
named above as the most visible flaw a set can have. There was nothing left in
the prompt to hold it.

Two lesser findings: Bing returns **JPEG**, which on a hard black-and-white mask
triples the midtone the alpha conversion has to guess about (2.66% against
0.92%), and the result reads paler and softer than the family at 16 px.

**So the requirement to check first in any second source is prompt length.** A
tool that cannot take 3700 characters cannot draw for this set, however good its
pictures are — and Bing's picture was faithful to its subject line. What did not
fit was the system around the subject.

## The review gate

**Nothing is accepted one icon at a time.** When a batch lands, it is rendered
as a contact sheet — every icon at 16 px, in one grid, on both the light and the
dark panel colour. That sheet is the only view in which the real failures are
visible: the one that is a grey smudge, the two that are indistinguishable, the
one whose weight is off.

Judging an icon at 1024 tells you nothing about the icon. It is going to be
drawn at 16.

It caught two failures in the first five icons, and the second is the more
instructive: `MeshPart` and `Part` arrived measuring perfectly — right keyline,
right fill, clean mask — and were the same icon at 16 px. `MeshPart` had been
specified as "a solid circle with three gaps cut across it", and three gaps from
a centre *is* the cube glyph. **At 16 px only the silhouette survives, so two
icons are distinguished by their outline or not at all** — which is why the
respecified `MeshPart` is a triangle and not a rounder cube.

The first failure was cruder. `Part` arrived as an outlined
wireframe cube: legible at full size, a faint grey thread at 16 px, and worse on
the light panel than the dark one because a thin dark line on a pale ground
reads thinner still. `Folder`, solid, is crisp at every size on both. The two
sat one above the other on the same sheet and there was nothing to argue about.

## Start with seven

**`Folder` · `Part` · `MeshPart` · `Model` · `Script` · `Camera` · `Workspace`**

### Three circular arrows, decided together at the sixth attempt

`RunService`, `HotReloadService` and `Refresh` were specified in three separate
sittings, and all three came out as a circular arrow. `Refresh` and `RunService`
measured **9.6%** apart — the same drawing under two names.

Decided as a group, which is what should have happened the first time:

| | |
|---|---|
| `Refresh` | keeps the plain open circular arrow — it is the most literal user of that glyph, and a browser refresh is what people expect it on |
| `HotReloadService` | the loop with a flame inside it. Already 24% from `Refresh`; the flame is doing real work |
| `RunService` | becomes a **solid stopwatch**. A tick is a clock, and a filled face against an open ring is the distinction that already separates `UIService` from `Frame` |

The general rule was written after the third instance and this is the sixth.
What makes it keep happening is that a domain is not a section of the table: the
three circular arrows sit in three different groups, and nothing but reading for
*shape* rather than for *category* would have found them together.

### The input trio, re-cut before it was drawn

The fourth instance of the error, and the first one caught before a pencil
moved. `InputContext` was specified as "a rounded rectangle with two small
square holes cut out of it" — which is a perfectly good icon and would have been
**the ninth rectangle** in a family that already has eight, drawn while the ink
on that family's re-cut was still wet.

`InputAction` (a lightning bolt) and `InputBinding` (a two-pronged plug) were
already fine: nothing else in either set is a bolt or a plug. Only the container
needed moving, and it moved to brackets — two shapes with a gap between them,
which is a silhouette this set does not otherwise own.

Reading the group before drawing cost about a minute. The same error caught
after drawing has cost, so far, a `Workspace` twice, an `AudioService`, and a
`PointLight`.

### The same specification error, three times

`Workspace` was a cube because `Part` had not been thought about yet.
`AudioService` became a mixing desk because `Sound` had already taken the
speaker. `PointLight` was given rays without noticing that `Lighting` is
*nothing but* rays.

All three are one mistake: **writing subject lines one at a time inside a domain
that has more than one member.** The second icon then gets chosen to dodge the
first rather than because it is right, and the third collides with something
nobody re-read.

The counter-discipline is cheap: before writing any subject line, look at what
else in the table shares its domain — lights, audio, documents, containers,
anything round — and decide the whole group in one sitting.

### The audio trio was assigned wrong, and the fix was to move all three

The first pass gave `Sound` the speaker, and `AudioService` then had to be
something else — it became a mixing desk, which reads as a barcode rather than
as audio. The mistake was assigning them one at a time: the second icon was
chosen to avoid the first rather than because it was right.

Reassigned as a group. **The speaker is the audio symbol, so the audio *system*
gets it.** `Sound` — one sound, seen far more often in a tree — becomes a
musical note, which no other outline here resembles. `AudioGroup` takes the
faders, which is what a group of audio channels literally is.

Three distinct silhouettes and three honest meanings, and it cost nothing: the
mixer already drawn for `AudioService` is exactly `AudioGroup`'s icon and only
needs renaming.

### The set can afford exactly one hexagon

`Part` has it. The first pass gave the cube glyph to three of the seven —
`Part`, `Workspace` with a bar under it, `Model` as three of them — and at 16 px
`Part` and `Workspace` were one icon separated by a 1.3 px line, the exact
weight the fill rule exists to reject.

The rule generalises: **whenever a subject could reasonably be drawn as a
container, a box or a thing-in-the-world, check first whether the set already
spends that silhouette.** Interior detail cannot rescue a shared outline,
because at 16 px there is no interior.

Most of what anybody actually looks at in a tree. Do these first, put them
through the contact sheet, and only then commit to the other thirty-five — a
style problem found at seven costs seven regenerations and found at forty-two
costs forty-two.

---

### The eight UI rectangles, decided as one group

Every one of them is honestly "a rectangle with something in it", and at 16 px
what is *in* a rectangle has stopped existing. So the family is separated by
**outline breaks and aspect**, never by interior detail:

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

**Two pairs are meant to resemble each other and that is not a defect.**
`TextButton` and `ImageButton` both carry the cursor, because the cursor means
*button* — the resemblance is information. Likewise the two `Image*` icons share
the picture symbol. What must not happen is `Frame` being indistinguishable from
`TextLabel`, and it is not: one is a bordered box and the other is two loose
bars.

This is how real editor sets behave. A family that reads as a family is right;
a family whose members cannot be told apart *across* categories is not.

## Services

Each appears exactly once, under `game`.

| File | Keyline | Subject line |
|---|---|---|
| `Workspace.png` | Circle | a solid filled circle with two narrow gaps cut across it — one straight and vertical, one bowed and horizontal — making a globe with a meridian and an equator. The cuts are the thin 35 px inner-cut weight, not a stroke. **No cube.** A solid disc is a silhouette nothing else in the set has: `Lighting` is spiky with rays, `RunService` and `HotReloadService` are open rings |
| `Lighting.png` | Circle | a filled circle with eight straight rays radiating outward from it, like a sun |
| `RunService.png` | Circle | a **solid stopwatch**: a filled circle with a small stem and crown on top breaking the outline, and one narrow hand cut out of the face. **Not an open circular arrow** — that is `Refresh`, and the two measured 9.6% apart when both were rings. A tick is a clock, and a solid face against an open ring is the same distinction that separates `UIService` from `Frame` |
| `ScriptService.png` | Wide | two solid document sheets **fanned apart at a clear angle**, the back one rotated so its corners project beyond the front one on both sides. The silhouette must be visibly WIDER than a single sheet — stacked nearly square, two documents have the outline of one, and `Script` already owns that outline |
| `HotReloadService.png` | Circle | a circular arrow loop with a small flame shape sitting inside the loop |
| `DebugService.png` | Tall | a beetle seen from directly above: a **large solid oval body** filling most of the height, with three SHORT legs on each side. The body has to dominate — long legs make it a radiating star, which is what `Lighting` already is, and the two measured 17% apart when it was drawn that way |
| `PhysicsService.png` | Square | a solid circle in the upper left **and** a thick curved arc sweeping down and away beneath it — a ball with the path it fell along. The ball is not decoration: an arc on its own is a swoosh that means nothing, which is what the first attempt was |
| `StreamingService.png` | Square | a three-by-three grid of solid filled square tiles with the four corner tiles removed |
| `TagService.png` | Wide | a solid luggage tag — a rectangle with one pointed end — with a single round hole cut out near the point. The point and the hole are what hold it apart from `Folder`, which is the same wide slab with a different corner interrupted; they measure only 17% apart and are obvious on the sheet |
| `TweenService.png` | Square | a smooth S-shaped ease curve rising from a dot at the lower left to a dot at the upper right |
| `UIService.png` | Wide | a solid rounded rectangle with a horizontal slot cut across it near the top, making a window with a title bar |
| `InputService.png` | Wide | a game controller silhouette seen from the front |
| `AudioService.png` | Wide | a solid speaker — a small rectangle joined to a trapezoid that widens to the right — with three thick curved arcs radiating from its wide end. The speaker is THE audio symbol and the service is the audio system, so it gets it |

## World and scene

| File | Keyline | Subject line |
|---|---|---|
| `Folder.png` | Wide | a classic file folder seen from the front, with a raised tab on its top left |
| `Model.png` | Square | a solid square with a second solid square offset up and to the right behind it, the two separated by a clear 85 px gap so both read. **No cube.** Grouping survives at 16 px as two overlapping bodies; three small copies of another icon do not |
| `Part.png` | Square | a solid filled hexagon with three narrow straight gaps cut from its centre outward to three alternating corners, forming a cube seen corner-on. Solid, not a wireframe |
| `MeshPart.png` | Circle | a solid upward-pointing triangle with a smaller upside-down triangle cut out of its centre, the classic subdivided-polygon symbol. **Its silhouette must be a triangle** — this is what separates it from `Part` at 16 px, where interior detail has stopped existing |
| `CharacterBody.png` | Tall | a standing person pictogram: a solid circle for the head and, below it, a **broad rounded torso that widens toward the bottom** — the shape on a door sign, without arms or legs. Not a capsule: three attempts at "a wide capsule" came back at 0.31, 0.37 and 0.33 wide for tall, because a capsule is a shape a model draws narrow. A person is a shape it draws wide |
| `Camera.png` | Wide | a solid rounded rectangle with a large circular hole cut out of its centre and a small square bump on its top edge |
| `Script.png` | Tall | a solid page with its top-right corner folded — the fold cut out so the black shows through — and three horizontal bars cut across the middle, left-aligned, the bottom one shorter. **Drawn in one chat with `ModuleScript.png`** and the two share a page to 0.1%; neither is reopened alone |
| `ModuleScript.png` | Tall | `Script.png`'s page exactly, with the three bars replaced by two facing angle brackets cut from the middle. The page is the shared half and the mark is the told-apart half, which is what makes them readable as one kind of thing at 16 px |
| `Material.png` | Square | a solid sphere with a small round highlight cut from its upper left, where a light would catch. **The material preview every engine shows is a lit sphere**, and the highlight is the one mark that says *surface* rather than *ball*. Measured 30.2% from `PointLight`, which was the pair to watch: the lamp's stem carries it |
| `Attachment.png` | Square | a small solid square with four short arms reaching from the middle of each edge — a named point on a surface with a frame attached. **Not a thin cross**: the filled centre and the short arms are what keep it 19.1% from `action.Add` instead of being it |
| `Bone.png` | Tall | a skeleton bone: two lobed ends joined by a narrower shaft. Nothing else in the set is lobed. **At 16 px its silhouette reads as a serif capital I** — known and accepted, since it is a bone at 24 and up and `content.Font` is a letter on purpose |
| `Ragdoll.png` | Tall | a figure hanging limp: a round head and limbs falling straight down rather than standing. The pose is the whole icon — it has to be tellable from `CharacterBody`, which stands, and from `AnimationPlayer`, which moves |
| `Weld.png` | Wide | two solid filled squares side by side, joined by a thick horizontal bar between them |
| `WeldConstraint.png` | Wide | two solid filled squares side by side and touching, with a single large round hole cut out where they meet |
| `PointLight.png` | Tall | a solid light bulb seen from the front: a round glass, a narrow neck, and a screw base beneath it. **No rays.** The bulb shape already says light, and rays would make it a radial blob — which is what `Lighting` is, and the two measured 21% apart when it had them |
| `SpotLight.png` | Tall | a small solid circle at the top and, **separated from it by a clear gap**, a wide triangle of light opening downward. The gap is the whole icon: fused to the circle, the triangle becomes one solid object with a round head — a chess pawn — and light is not an object |
| `Sound.png` | Tall | a solid musical note: a filled oval note head, low and to the left, with a thick straight stem rising from its right side and a flag at the top. A note is instantly readable at 16 px and shares its outline with nothing else here |
| `AudioGroup.png` | Square | three thick vertical bars side by side, each with a square notch cut out at a different height, like the faders of a mixing desk. A group of audio channels IS a set of faders |
| `AnimationPlayer.png` | Wide | **one large solid diamond** in the centre, keyframe-shaped, with a thin horizontal line passing behind it and a much smaller diamond at each end. The big diamond is the icon; the line is a secondary mark. Drawn the other way round — a line with three equal diamonds on it — it is the palest thing in the set on both panels, because it is a line icon with no body anywhere to carry weight |

## User interface

They share the Wide keyline on purpose: a column of UI icons that all sit on one
proportion reads as a group before anybody has looked at what is inside them.
They are also where the fill rule does the most work — every one of these is a
solid rectangle with its detail cut out of it, not a thin box with things drawn
inside.

| File | Keyline | Subject line |
|---|---|---|
| `ScreenGui.png` | Wide | a solid rectangle with a **short stand and a foot beneath it** — a monitor. The stand is what breaks the outline: every other icon in this family is a bare rectangle, and this one is a rectangle standing on something |
| `Frame.png` | Wide | a solid rounded rectangle with a large rectangular hole cut out of its centre, leaving a thick even border. **The plainest shape in the family, deliberately** — a `Frame` is the plain container, so it earns the plain outline and every sibling below differs from it by an added break |
| `TextLabel.png` | Wide | **no container at all** — three horizontal bars stacked, the first two long and the third clearly short, like the last line of a paragraph. **The block of three must fill the Wide keyline, 854 by 683**, which means shorter bars and a taller stack than instinct suggests: two long bars alone are a very wide sliver that reads as an equals sign, not as text |
| `TextButton.png` | Wide | a **solid filled** rounded rectangle — no hole — with a mouse cursor arrow overhanging its lower-right corner. Filled where `Frame` is hollow, and the cursor is the family's mark for *button* |
| `TextInput.png` | Wide | a solid rectangle with a short horizontal bar cut out of it, and a **text I-beam caret — a vertical bar with a short serif at its top and bottom — standing across the LEFT edge**, half inside the rectangle and half outside. The I-beam straddling the edge is both the outline break and the universal mark for typing. **Not a stem above the box:** a caret rising from the top edge reads as a sign on a post |
| `ImageLabel.png` | Wide | a solid rectangle with a triangular mountain and a small circular sun cut out of it, the standard picture symbol. The mountain notch is deep enough to read at 16 px, which is what separates it from `Frame` |
| `ImageButton.png` | Wide | the picture rectangle — mountain and sun cut out — with a mouse cursor arrow overhanging its lower-right corner |
| `ScrollFrame.png` | Tall | a **tall** rectangle with a thick border, and inside it against the right edge a short solid bar sitting near the top — a scroll thumb at the top of its track. **Nothing protrudes past the outline:** a rounded bar sticking out of the right side is the silhouette of a phone's power button, which is what the first attempt looked like. Its aspect already separates it from `Frame`; the thumb only has to confirm it |
| `UICorner.png` | Square | an **L-shaped corner bracket**: two thick bars meeting at a generously rounded elbow, open on the other two sides, drawn at the 120 line weight. The rounded elbow IS the subject. **Not a square with one corner rounded** — that measured **2.0%** from the transport `Stop`, the lowest number in this project, because three sharp corners against four soft ones is two pixels at 16 px. An L is a silhouette nothing else in any set has |
| `UIListLayout.png` | Square | a **thick vertical bar down the left side** and three short horizontal bars to the right of it, evenly spaced with a clear gap from the rail — items arranged along an axis, which is what a layout is. The rail is what separates it from `TextLabel`'s loose bars, and it is also what lets the block sit near its keyline: three stacked *squares* are 1:3 by construction and no keyline in this system has a band for that |
| `UIPadding.png` | Square | four separate thick bars arranged as the four sides of a square, with a clear gap at each of the four corners |

## Input

| File | Keyline | Subject line |
|---|---|---|
| `InputContext.png` | Square | **two thick square brackets facing each other** — `[` and `]` — with a clear gap between them and nothing inside. A context is a scope, and brackets are what a scope looks like. **Not a rectangle:** it was specified as a rounded rectangle with two holes, which would have been the ninth rectangle in a set that already has eight |
| `InputAction.png` | Tall | a lightning bolt |
| `InputBinding.png` | Tall | a two-pronged electrical plug seen from the front |

## Fallback

| File | Keyline | Subject line |
|---|---|---|
| `Instance.png` | Circle | a solid circle with a ring cut out of it, leaving a dot standing at the centre |

Drawn last and used first: it is what the Explorer shows for a class with no
icon of its own, so it has to look deliberate rather than missing.

---

## What the engine still needs

The art is half of it. These are the pieces on the code side, checked against
the tree rather than assumed, and they are what turn a folder into a pipeline.

**1. A lossless path for UI textures.** `assetc` sends every image through
basisu to KTX2/UASTC with generated mips (`tools/assetc/src/texture.cpp`). That
is right for a world texture and wrong for a 16-pixel icon: block compression
smears small alpha shapes, and a UI icon has nothing but small alpha shapes. UI
art needs a route that stays lossless.

**2. An atlas.** There is no packer. Forty-two icons as forty-two textures is
forty-two binds; every editor packs them into one sheet with a UV table, and one
bind is what a UI wants.

**3. A generated registry.** This repository already generates C++ from data —
`api/generator/gen_cpp.luau` produces the `*.gen.cpp` class descriptors. An
icon registry built the same way, from this directory's listing, buys the thing
string lookup cannot: **a missing icon is a build error rather than a blank
square at runtime**, and a typo does not compile. It also means the list of
classes and the list of icons cannot silently drift apart.

**4. Tint at draw time.** The master is a mask, so the editor multiplies it by a
theme colour when it draws. One asset, both themes, no second set to maintain
and nothing to keep in sync.

**5. Sizes.** 16 and 20 at 1×, doubled for high DPI. Generated from the master,
never drawn by hand.

**Already solved:** ImGui takes a native texture handle directly — the debug
overlay already draws one (`engine/app/src/debug_overlay.cpp:724`). Binding is
not part of the work.

## What happens to the masters

Nothing by hand. The delivered PNG is the source, and one batch step does all of
it: the white-on-black mask becomes alpha, the shape is trimmed and re-centred
against its keyline, the sizes are generated, and the atlas and registry are
built from the result.

Hand-trimming one icon puts it two pixels off and the whole column looks wrong
without anybody being able to say why. That is the failure this pipeline exists
to make impossible, not merely unlikely.

## Licensing

Original artwork commissioned for this project, covered by the repository's own
licence, and therefore **not** in `THIRD_PARTY_NOTICES.md` — that file is for
other people's work.

The distinction matters in one direction only: anything pulled from an icon set,
a stock library or someone else's repository is third-party however small, needs
a permissive licence (R6), and needs recording. Do not mix the two sources inside
this directory.
