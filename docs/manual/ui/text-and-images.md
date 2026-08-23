# Text and images

## TextLabel

```luau
--!strict
local title = Instance.new("TextLabel")
title.Size = UDim2.new(1, -32, 0, 48)
title.Text = "Score: 0"
title.TextSize = 40
title.TextColor = Color3.fromRGB(240, 236, 224)
title.HorizontalAlignment = Enum.HorizontalAlignment.Center
title.VerticalAlignment = Enum.VerticalAlignment.Center
title.BackgroundTransparency = 1
title.Parent = screen
```

| Property | Default | Notes |
|---|---|---|
| `TextLabel.Text` | `""` | UTF-8. |
| `TextLabel.TextColor` | black | **No `3` suffix.** |
| `TextLabel.TextSize` | 14 | The em size **in pixels**. |
| `TextLabel.Font` | `""` | An `asset://` URI to a TrueType file. |
| `TextLabel.HorizontalAlignment` | `Center` | |
| `TextLabel.VerticalAlignment` | `Center` | |
| `TextLabel.TextWrapped` | `false` | |
| `TextLabel.TextScaled` | `false` | |

**The alignment properties are shared with layout.** They are
`HorizontalAlignment` and `VerticalAlignment`, the same enums a `UIListLayout`
uses — there is deliberately no `TextXAlignment` beside them, and no runtime
alias for one.

A codepoint the font has no glyph for draws the font's undefined-character box
rather than nothing: a label that silently drops characters is the failure mode
that ships.

## Fonts

`Font` empty is the engine's own default face, and so is the literal name
`Inter`. A name that cannot be resolved falls back to the default and says so
**once**, rather than once a frame.

A build whose content directory has no font at all falls back further, to a
built-in face that is ASCII only, one weight and no kerning — text still draws,
which is the point of having a fallback.

Glyphs are rasterised on demand and cached by face, size and codepoint, so a
`TextSize` a tween is animating costs one cache entry per quarter-pixel rather
than one per frame.

**`TextSize` is in window pixels and is not scaled for you.**
`UIService.DisplayScale` is what a game multiplies by when it wants text to be
the same physical size on two displays.

## Wrapping and scaling

`TextWrapped` breaks lines at the box's width. It breaks at spaces, and
**mid-word only for a word wider than the box** — a word cut in half at a random
letter is worse than one that overhangs.

`TextScaled` ignores `TextSize` and rasterises at whatever size fills the box,
preserving aspect. **Re-rasterised rather than stretched**: there is no distance
field here, and a stretched bitmap is what "scaled text" usually means and looks
like.

## TextInput

`TextInput` extends `TextLabel` and adds a single-line editable field: typed
text, backspace and a caret. There is no selection, no clipboard, no undo, and
no composition beyond what the platform delivers as text.

```luau
--!strict
local field = Instance.new("TextInput")
field.Size = UDim2.new(0, 220, 0, 32)
field.PlaceholderText = "your name"
field.Parent = screen

field.Focused:Connect(function()
    print("typing here now")
end)

field:GetPropertyChangedSignal("Text"):Connect(function()
    print(field.Text)
end)
```

`TextInput.PlaceholderText` is drawn dimmed in place of `Text` while the field
is empty and unfocused.

**Focus is taken by a press and released by a press elsewhere**, and there is
deliberately no settable focused property — two fields could then both believe
they had it. `TextInput.Focused` and `TextInput.FocusLost` are the events.

Typing writes `Text` through the same path a script assignment uses, so
`Instance.GetPropertyChangedSignal` fires for it. A focused field also takes the
keyboard away from the Input Action System, so movement keys do not fire while
somebody is typing.

## ImageLabel

```luau
--!strict
local panel = Instance.new("ImageLabel")
panel.Size = UDim2.new(0, 240, 0, 96)
panel.Image = "asset://ui/panel.png"
panel.ScaleType = Enum.ScaleType.Slice
panel.SliceCenter = Rect.new(Vector2.new(12, 12), Vector2.new(52, 52))
panel.BackgroundTransparency = 1
panel.Parent = screen
```

| Property | Default | Notes |
|---|---|---|
| `ImageLabel.Image` | `""` | A compiled texture or an ordinary image file. |
| `ImageLabel.ImageColor` | white | Multiplied into the image. |
| `ImageLabel.ScaleType` | `Stretch` | |
| `ImageLabel.SliceCenter` | empty | Where the nine-slice cuts are, in **source pixels**. |

`ImageColor` multiplies, so one white glyph sheet draws in any colour.

An empty `Image` draws nothing but still fills the background. A URI that names
nothing and a picture still loading both draw as a flat `ImageColor` rectangle
— the same thing, because from a frame's point of view they are.

`Enum.ScaleType` has exactly three items:

- **`Stretch`** — the whole image into the whole box. Aspect ratio is **not**
  preserved.
- **`Slice`** — nine-slice: the corners at their own size, the edges stretching
  along one axis, the middle along both. This is how a panel keeps its rounded
  corners at any size.
- **`Tile`** — repeated at its own size until the box is full. Bounded: past
  about four thousand tiles it stretches instead, because a one-pixel image
  tiled over a full-screen frame is nearly a million quads.

`SliceCenter` is kept as given rather than corrected. A slice quietly fixed up
is a panel that draws wrong with no way to find out why. A box narrower than its
own two corners collapses the middle rather than drawing the corners over each
other.

## The button classes

`TextButton` extends `TextLabel` and `ImageButton` extends `ImageLabel`, and
**neither adds a property**. `UIObject.Activated`, `UIObject.PointerEntered` and
`UIObject.PointerExited` live on `UIObject`, because anything can be pressed —
including a plain `Frame`.

The classes exist because they are the ones a reader recognises.

## What is not here

No rich text — colour, weight and size do not vary inside one label, and complex
scripts are not shaped. No world-space UI: this tree draws on the screen and
nowhere else.

## Where to look next

- [Buttons and interaction](manual:ui/interaction)
- [Content and asset URNs](manual:assets/content) — what an `asset://` URI is
- [`TextLabel`](api:TextLabel) · [`ImageLabel`](api:ImageLabel)
