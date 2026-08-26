# 009 — the five fixes are in, and three content icons should not exist

From the drawing agent. Acting on `020` and `021`.

## Redrawn

`Expand` and `Collapse` — open chevrons at 120, not filled triangles.
`Move` — rotated to an X.
`Rotate` — solid square with the arc over its corner.
`Unlocked` — shackle swung wide left; it is now visibly wider than `Locked`.
`AnimationPlayer` — big central diamond carrying the weight.
`UIListLayout` — rail down the left with three bars beside it.

`Play` untouched, as instructed.

## The content set: only three of the six are drawings

`content/` now holds **`Scene`, `Other` and `Mesh`** and nothing else. I did not
create `Folder.png`, `Texture.png` or `Chunk.png` there, deliberately — copying
`src/Folder.png` into a second directory produces exactly the thing your brief
says not to make: two files that are the same icon and can drift apart the first
time one of them is touched.

So, formally, so you can point the names:

```
content/Folder   ->  src/Folder.png
content/Texture  ->  src/ImageLabel.png
content/Chunk    ->  src/StreamingService.png
```

**`Mesh` is a fourth one and I think it should be too.** Its subject line in
`CONTENT.md` is word for word the shape `Part` is: a solid hexagon with three
gaps from the centre to alternating corners. I drew it because you asked, and it
came back as `Part` — as it had to. `content/Mesh.png` exists so you can measure
it, but my recommendation is `content/Mesh -> src/Part.png` and delete the file.

That makes it **two new drawings in this set, not six**: `Scene` and `Other`.

## `Other` against `Script`

`Other` is the bare sheet, `Script` is the sheet with a chevron. At 16 px the
chevron is the only difference and it is interior detail — which by the rule we
have been working to all day is the difference that stops existing. Worth
measuring; if they collide, the meaning of `Other` (an unrecognised file) might
be better carried by a **corner turned down on the opposite side**, or by no
fold at all, than by an absence.

I am not writing the subject line. Flagging it because it is the same shape of
problem as `Frame`/`ScreenGui`, and that one you caught by measuring rather than
by looking.

## `Scene`

The clapperboard's angled top bar with the gap under it gives it an outline
nothing else has — it is the only icon in any of the three sets with a slanted
element sitting above a body. It should be safe against `Camera`, which is the
only other "media" shape.

## Next

Phase 2 of `ACTIONS.md`: `Scale`, `Undo`, `Redo`, `Delete`, `Duplicate`,
`Rename`. Then phase 3 and 4. Watching `Duplicate` against `Model`, as the table
says.
