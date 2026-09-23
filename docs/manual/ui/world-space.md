# UI in the world

A `ScreenGui` draws over the world. Two other roots draw **in** it: a
`SurfaceGui` on a face of a part, and a `BillboardGui` hung over a point and
turned to the camera. Everything under them is the screen's own classes —
`Frame`, `TextLabel` (rich text included), `ImageLabel`, `UICorner`,
`UIListLayout` — laid out the same way. The difference is where the result
goes: into the world's picture, so what stands in front of a sign hides it.

## A screen on a wall

```luau
--!strict
local wall = Instance.new("Part")
wall.Anchored = true
wall.Size = vector.create(10, 6, 0.5)
wall.Position = vector.create(0, 3, -8)
wall.Parent = workspace

local board = Instance.new("SurfaceGui")
board.Face = Enum.Face.Front
board.Parent = wall

local title = Instance.new("TextLabel")
title.Size = UDim2.new(1, 0, 0, 80)
title.BackgroundTransparency = 1
title.TextColor = Color3.fromRGB(240, 240, 240)
title.TextSize = 52
title.RichText = true
title.Text = "<b>ROUND 3</b>  <font color=\"#ffb347\">02:17</font>"
title.Parent = board
```

A surface's canvas is its face measured at `PixelsPerMetre`, fifty by default:
the ten-by-six-metre face above is a 500 by 300 canvas, and a `TextSize` of 52
is about a metre tall on the wall. More pixels to the metre is finer text on the
same face.

`Face` names the side from the part's own point of view. `Front` is the one its
`LookVector` comes out of, the four sides are read the right way up, and the top
is read with the part's front at its top edge.

## A name over a head

```luau
local tag = Instance.new("BillboardGui")
tag.Size = UDim2.new(0, 180, 0, 54)
tag.WorldOffset = vector.create(0, 2.2, 0)
tag.Parent = character -- a part; or set Adornee

local name = Instance.new("TextLabel")
name.Size = UDim2.new(1, 0, 0, 30)
name.Text = "Scout"
name.Parent = tag
```

**A billboard's `Size` means two things at once.** Its scale is metres: a
billboard sized `UDim2.new(4, 0, 1, 0)` is a four-by-one-metre board that
shrinks with distance like anything else. Its offset is pixels: the tag above is
180 by 54 pixels on the screen whether its owner is next to you or across the
map. Children lay out against the pixel size either way.

`WorldOffset` is in the world's axes, so `vector.create(0, 2.2, 0)` is above
the part however the part turns. `MaxDistance` hides it past a range, and
`AlwaysOnTop` lets it show through walls.

## Brightness

These trees are drawn in the world's light, which is brighter than the
screen's: a colour is converted from the screen's sRGB into linear light and
scaled by `Brightness`. At one, white reads as a lit white surface; above one it
glows and blooms, which is what a screen on a wall usually wants.

## Pressing a button in the world

A `TextButton` on a `SurfaceGui` or a `BillboardGui` fires `Activated`, and the
hover events, exactly as one on the screen does. The engine casts the pointer's
ray into the world. It finds the canvas the ray meets and the element under
that point, in the canvas's own pixels:

- **Something solid in front hides it.** A crate between the camera and the
  scoreboard takes the click. The part the canvas is printed on never counts.
- **`AlwaysOnTop` is drawn over everything and is pressed over everything.**
- **A canvas is read from its front.** The back of a sign is not a button.
- **The screen comes first.** A screen element over the same pixel wins,
  because it is drawn in front of the world.
- **Empty canvas is not a hit.** A click on a canvas's transparent space goes
  on to whatever is behind it.

## What is not here

A `ClipsDescendants` frame in the world clips upright children and not turned
ones.

## Where to look next

- [The UI tree](manual:ui/tree)
- [Text and images](manual:ui/text-and-images)
