# The UI tree

Screen-space UI is an instance tree like everything else, parented under
`UIService` and drawn over the world.

```luau
--!strict
local UIService = game:GetService("UIService")

local screen = Instance.new("ScreenGui")
screen.Name = "Hud"
screen.Parent = UIService

local bar = Instance.new("Frame")
bar.Size = UDim2.new(1, 0, 0, 44)
bar.BackgroundColor = Color3.fromRGB(18, 20, 28)
bar.BackgroundTransparency = 0.35
bar.Parent = screen
```

`UIService` takes the role a player's GUI container plays elsewhere, without
taking the player with it — there is no players service here, and a UI does not
need one.

## ScreenGui is the unit of layout

A `ScreenGui` is the root of one tree. A write that changes a layout marks its
nearest `ScreenGui` dirty, and **a screen nothing changed does not run the
solver at all**. That is a design constraint rather than an optimisation: the
benchmark asserts zero solver invocations on an idle frame.

| Property | Default | Means |
|---|---|---|
| `ScreenGui.Enabled` | `true` | Whether this tree is laid out and drawn. A disabled one costs a boolean read, not a layout. |
| `ScreenGui.DisplayOrder` | 0 | Which tree draws on top when two overlap. Highest last. |
| `ScreenGui.ScreenInsets` | `true` | Whether it lays out inside the safe area rather than against the whole window. |

`DisplayOrder` orders *between* trees; `UIObject.ZIndex` orders *within* one. So
a HUD and a modal never have to agree on a shared numbering — give the modal a
higher `DisplayOrder` and it is simply on top.

`ScreenInsets` is on by default because a full-bleed background is the exception
and a HUD clipped by a notch is the failure.

## UIObject is the base

`UIObject` is abstract and is everything that occupies a rectangle: `Frame`,
`TextLabel`, `TextButton`, `TextInput`, `ImageLabel`, `ImageButton`,
`ScrollFrame`. Every property on it is one the layout or the 2D pass reads,
which is what makes it the base rather than a convenience.

| Property | Default | Means |
|---|---|---|
| `UIObject.Position` | `UDim2.new()` | Where this object's anchor point sits in its parent. |
| `UIObject.Size` | `UDim2.new()` | Its size. Overridden on an axis `AutomaticSize` covers. |
| `UIObject.AnchorPoint` | `Vector2.zero` | Which point of **itself** `Position` places. |
| `UIObject.BackgroundColor` | white | No `3` suffix. |
| `UIObject.BackgroundTransparency` | 0 | 0 opaque, 1 invisible. |
| `UIObject.Visible` | `true` | Laid out, drawn **and** hit-tested. All three together. |
| `UIObject.ZIndex` | 0 | Draw order within this tree. Flat, not nested. |
| `UIObject.LayoutOrder` | 0 | Read only by a `UIListLayout` sorting by it. |
| `UIObject.AutomaticSize` | `None` | Which axes size to fit contents. |
| `UIObject.ClipsDescendants` | `false` | Whether children are clipped to this rectangle. |
| `UIObject.Rotation` | 0 | Degrees clockwise about the anchor point. |
| `UIObject.AbsolutePosition` | — | Read-only output: the top-left in window pixels. |
| `UIObject.AbsoluteSize` | — | Read-only output. |

Two of those are worth stating plainly:

- **A fully transparent background still lays out and still hit-tests.**
  `Visible` is the property that stops both.
- **`ZIndex` does not nest.** One flat ordering across the tree, ties broken by
  document order, because a per-parent stacking context is the part of CSS
  nobody can hold in their head.

## Modifiers are siblings, not containers

`UIListLayout`, `UIPadding` and `UICorner` extend `Instance` rather than
`UIObject`. They are **parented beside** what they affect:

```luau
local layout = Instance.new("UIListLayout")
layout.Parent = menu      -- arranges menu's children; menu keeps them
```

Adding one does not reparent anything. Only the **first** `UIPadding` and the
**first** `UIListLayout` among a parent's children are read; a second of either
is ignored.

## The screen itself

`UIService` publishes two read-only numbers, both rewritten by the engine every
frame:

- `UIService.DisplayScale` — the window's pixel density relative to its logical
  size; 2 on a doubled display. **UI coordinates are in pixels**, so this is
  what a game multiplies by when it wants a measurement to mean the same
  physical size on two screens.
- `UIService.SafeAreaInsets` — how far in from each window edge it is safe to
  draw. Zero on a desktop window.

`SafeAreaInsets` is a `Rect`, and it is **not two corners**: it is four inset
distances. `Min` is (left, top) and `Max` is (right, bottom), so
`SafeAreaInsets.Max.X` is the inset from the right edge rather than an
x-coordinate.

## Where to look next

- [Layout with UDim2](manual:ui/layout) — the arithmetic
- [Buttons and interaction](manual:ui/interaction)
- [`UIObject`](api:UIObject) · [`ScreenGui`](api:ScreenGui)
