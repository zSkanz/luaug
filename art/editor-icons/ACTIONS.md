# Toolbar and action icons

The second set. `src/` holds one icon per **class** the Explorer can show;
[`actions/`](actions/) holds the editor's own chrome — the things a person
clicks rather than the things a scene contains.

**Same style block, unedited.** Read [`PROMPT.md`](PROMPT.md) exactly as for a
class icon: solid white on black, the keyline grid, 85 for strokes and gaps, 35
for a cut inside a solid, 120 for an icon that is a line rather than a body, and
no text. A gear drawn at a different weight from a folder is worse than a gear
drawn badly — the two sets sit in the same window.

Deliver into `actions/`, named exactly as the table says.

## A toggle pair shares a body

`Locked` and `Unlocked` are two states of one control, and a person reads them
by spotting **what changed**. That only works if everything else did not.

The first `Unlocked` moved the shackle *and* made the body narrower, taller,
shifted to the right and less rounded. Each of those is defensible alone;
together they stop the two icons being one object in two states and make them
two different padlocks, which tells a person nothing about which state they are
in.

**Only the part that carries the state may change.** The rule is worth stating
because it is invisible in either icon on its own — it is a property of the
pair, and it is why this one had to be seen side by side to be caught.

## Three of these were the same mistake, again

`Expand` and `Play` were given **the same subject line** — "a solid triangle
pointing right" — and came back as the same icon. `Add` and `Move` were both
cross shapes. `Locked` and `Unlocked` differed by a detail too small to see.

All three are the error this project has now made five times: **writing subject
lines one at a time inside a group.** It happened here even though the class set
had just been re-cut twice for exactly this, because a new table felt like a
fresh start rather than the same trap.

`Locked` / `Unlocked` is the one that would have hurt most. They are the two
states of one toggle, so a person has to see *which state the button is in* —
two icons 9.9% apart do not tell them.

## They must not collide with the class icons either

The checker compares **both sets together**, because both are on screen at once.
Three risks are visible before anything is drawn:

| | against | why |
|---|---|---|
| `Play` | `MeshPart` | both triangles — `Play` points right, `MeshPart` points up, and that had better be enough |
| `Add` | `StreamingService` | `StreamingService` is a plus made of squares; a plain plus is very close |
| `Visible` | `Camera` | both a body with a circle in the middle |

Draw those three early rather than late.

---

## Phase 1 — what E1 needs to be usable

| File | Keyline | Subject line |
|---|---|---|
| `Settings.png` | Circle | a solid gear: a thick ring with six or eight square teeth around its outside and a round hole cut through its centre |
| `Search.png` | Circle | a magnifying glass: a thick ring with a short thick handle leaving its lower right at 45 degrees |
| `Close.png` | Square | two thick bars crossing at the centre in an X, at 45 degrees |
| `Expand.png` | Square | a **chevron** pointing right — two thick bars meeting at a point, drawn at the **120 line weight and NOT filled**. A tree chevron is conventionally an open arrowhead where a transport `Play` is a solid triangle, and that is the difference at 16 px. They were specified as the same drawing and measured 4.3% apart |
| `Collapse.png` | Square | the same open chevron, pointing down |
| `Visible.png` | Wide | an eye: a solid pointed oval, wide, with a round hole cut through its centre |
| `Hidden.png` | Wide | the same eye with a thick bar struck through it diagonally, the bar separated from the eye by a clear gap on both sides so it reads as struck through rather than merged |
| `Locked.png` | Tall | a solid padlock: a rounded body below and a thick shackle arching above it, the shackle open in the middle so the arch reads |
| `Unlocked.png` | Tall | **the body of `Locked`, unchanged in every dimension and every corner radius**, with only the shackle moved: lifted clear and swung open to the left. The keyhole stays where it is. **The body must be identical** — drawn narrower, taller and shifted across, as the first attempt was, the pair stops reading as one padlock in two states and becomes two different padlocks |

## Phase 2 — E2, editing the scene

| File | Keyline | Subject line |
|---|---|---|
| `Move.png` | Square | four thick arrows pointing to the four **corners** — up-left, up-right, down-left, down-right — from a common centre. **Diagonal, not upright:** drawn on the vertical and horizontal it is a plus, arrowheads vanish at 16 px, and it measured 9.5% from `Add`. Rotated 45 degrees it is an X and nothing else in either set is |
| `Rotate.png` | Square | a **solid square in the centre** with a circular arrow running **all the way around it**, arrowhead at one end and a clear gap between the arrow and the square. **Centred, not cornered:** an arc over one corner leaves half the icon empty and the whole thing lopsided. It does not collide with `RunService` — that is a hollow ring, and this is a ring with a solid body inside it, which is a different silhouette entirely |
| `Scale.png` | Square | a small solid square at the lower left and a larger solid square at the upper right, joined by a thick diagonal bar between their nearest corners |
| `Undo.png` | Wide | a thick arrow curving to the left and back on itself, arrowhead at the left end. **120 line weight** |
| `Redo.png` | Wide | the same arrow mirrored, curving to the right, arrowhead at the right end. **120 line weight** |
| `Delete.png` | Tall | a rubbish bin: a solid tapered body with two or three narrow vertical slots cut out of it, a separate lid bar above it, and a small handle on the lid |
| `Duplicate.png` | Square | two solid squares offset diagonally with a clear gap between them. **Check against `Model`, which is the same idea** — if they collide, this one gains a plus in its corner and `Model` keeps the plain form |
| `Add.png` | Square | two thick bars crossing at the centre in a plus, upright, not rotated |
| `Rename.png` | Wide | a pencil lying at 45 degrees: a long solid body with a pointed tip at the lower left and a squared-off end at the upper right |

## Phase 3 — E3 and E4, saving and playing

| File | Keyline | Subject line |
|---|---|---|
| `Save.png` | Square | a floppy disk: a solid square with one corner clipped, a rectangular hole cut out of its lower half, and a smaller solid rectangle standing in its upper half |
| `Open.png` | Wide | a folder with its front panel tilted open, so the outline is a folder whose top edge slopes up to the right |
| `New.png` | Tall | a document sheet with a folded top-right corner and a plus cut out of its centre |
| `Play.png` | Square | a solid triangle pointing **right**, with a flat vertical left edge |
| `Pause.png` | Square | two thick upright bars side by side with a clear gap between them |
| `Stop.png` | Square | one solid square with slightly rounded corners |

## Phase 4 — E5, the asset browser

| File | Keyline | Subject line |
|---|---|---|
| `Back.png` | Square | a thick arrow pointing left: a triangular head and a straight shaft |
| `Forward.png` | Square | the same arrow pointing right |
| `Up.png` | Square | the same arrow pointing up |
| `Refresh.png` | Circle | a circular arrow forming a nearly closed loop with one arrowhead. **Check against `RunService` and `HotReloadService`, which are both this shape** — if it collides, this one is dropped rather than redrawn; a browser can use a menu item |
| `Grid.png` | Square | four solid squares in a two-by-two arrangement with even gaps |
| `List.png` | Square | three solid squares stacked with even gaps. **Check against `UIListLayout`, which is the same drawing** — if it collides, they are the same icon and should share one file rather than pretend to differ |

## Two of these may turn out to be duplicates, and that is fine

`List` and `UIListLayout`, `Refresh` and `HotReloadService` — if a pair measures
as one icon, the honest answer is that it **is** one icon, used twice. Two files
drawn to look slightly different so they can have two names is worse than one
file referenced from two places.

---

## The compressed block — Bing only, and only for the last fourteen

Written by the drawing agent, at the reviewer's request in `communication`
message `025`, and recorded here because a prompt that only one agent has is the
thing this project's protocol exists to prevent.

**This is not Part A and it must never be used in its place.** `PROMPT.md` is
the block for everything drawn in ChatGPT — all 42 in `src/`, the first 16 in
`actions/`, and `Scene` and `Other` in `content/`. This block exists for one
reason: Bing Image Creator's prompt field is capped at **480 characters** and
Part A is 3668, so the last fourteen action icons could not be drawn there any
other way.

**The block, verbatim — 335 characters.** Revised; see *Why this replaced the first version* below.

```
Flat UI icon. ONE solid pure white shape (#FFFFFF) on pure black (#000000), only 2 colours: no grey, gradient, shadow, texture. Flat, no 3D, no bevel. Inner detail is a HOLE cut out of the solid, never a thin outline. Thick strokes, MAX 4 SHAPES, nothing else in the frame, no border, no pattern. Centred, fills 80%. NO TEXT. Subject: 
```

Append the subject line from the table above, then a full stop, then **one** of:

```
 Tall shape.     Wide shape.     Square shape.     Round shape.
```

That last sentence is an addition of mine, not of the reviewer's rewritten
lines. It is two words and it is the only part of the keyline system that
survives the character budget — a shape's *proportion* is the thing normalisation
can never fix afterwards, and it cost eight characters to keep. Every one of the
fourteen fits with room to spare; `Save` is the longest at 472.

### What this block drops, so nobody has to diff it

Against Part A it loses: the keyline grid and the four keyline sizes, the
85 / 35 / 120 weights, the corner-radius constant, the minimum gap between
separate parts, the four-shape complexity limit, and the flat-cube exception.

It keeps: two colours only, flat silhouette, **fill and not outline**, inner
detail as a cut-out, heavy strokes, legibility at 16 px, centred at 80%, and no
text.

**The corner radius is the one that will show**, which is why every subject line
in the table above now says *rounded* in its own words. The block used to carry
that and cannot any more, so the subject carries it.

### Bing settings

Aspect ratio **1:1**. Model **Ilustração estilizada / MAI-Image-2e**. Output is
**JPEG**, not PNG — the mask step applies a levels pass (clamp below 12% to
black, above 88% to white, rescale the middle) which closes the gap to a
ChatGPT PNG.


### The six object subject lines, shortened to fit this block

Rewritten by the reviewer in `communication` message `029` so the budget is not
tight. Use these, not the longer versions in the table above.

```
Save     a floppy disk, rounded, one clipped corner, a hole cut in its lower half, a solid rectangle above it. Square shape.
Delete   a tapered bin, rounded, 3 narrow slots cut out, a separate lid bar above with a handle. Tall shape.
Rename   a solid pencil at 45 degrees, rounded, pointed tip lower-left, squared end upper-right. Square shape.
Open     a folder with its front panel tilted open, rounded, top edge sloping up right. Wide shape.
New      a document sheet, rounded, folded top-right corner, a plus cut from its centre. Tall shape.
Refresh  a thick circular arrow, nearly closed, one arrowhead, rounded ends. Round shape.
```

Worst line is now `Save` at 450 of 480 — thirty characters of headroom instead of
three. **If a subject needs more room, take it from the subject, never from the
block.** The block is the part that is identical for all fourteen, so a clause
dropped there costs fourteen icons. That is not hypothetical; see below.

### Why this replaced the first version

The block recorded here at first was 330 characters and read
`Thick strokes, nothing hairline, reads at 16px.` It had been squeezed down from
a 464-character test prompt to make room for long subject lines, and the clause
that paid for that room was **`max 4 shapes`**.

That is the only clause in the whole block that stops the model *adding* things.
Everything else describes the icon; that one describes what must not be there.
With it gone, the first `Delete` came back as a good bin **inside an ornamental
border** filling the frame.

So the `Delete` that was approved and the `Delete` that came back with scrollwork
were drawn from **different prompts**, and the difference was one clause nobody
had noticed leaving.

Kept as a rule rather than as a fix: **when a brief must be compressed, the
clause that forbids additions is worth more than any clause that refines what is
drawn.**
