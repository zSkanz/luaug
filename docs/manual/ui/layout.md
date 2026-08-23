# Layout with UDim2

Layout here is **arithmetic, not a constraint solve**. A child's rectangle is
two multiplies and an add per axis, in one pass down the tree:

```text
absSize = parentSize * Size.Scale     + Size.Offset
absPos  = parentPos  + parentSize * Position.Scale + Position.Offset
                     - AnchorPoint * absSize
```

There is nothing to solve: no constraint is under-determined, no sibling affects
another, and no iteration converges. That is why the layout is predictable and
why it costs nothing on a frame that changed nothing.

## UDim and UDim2

A `UDim` is one axis: a `UDim.Scale` fraction of the parent plus a `UDim.Offset`
in pixels. A `UDim2` is two of them.

```luau
frame.Size = UDim2.new(0.5, 0, 0, 40)   -- half the parent wide, 40 px tall
frame.Position = UDim2.fromScale(0.5, 0.5)
```

`UDim2.new` takes four numbers in axis order — x scale, x offset, y scale, y
offset. There is deliberately no second overload taking two `UDim`s: one
constructor with one argument order is what a frozen surface is for.
`UDim2.fromScale` and `UDim2.fromOffset` cover the common cases.

**`Scale` is not clamped.** A value past 1 or below 0 is legal and means what it
says. `UDim` and `UDim2` support `+`, `-` and `==`, and nothing else — there is
no scalar multiply.

Both are immutable: `UDim2.X` and `UDim2.Y` are read-only. Build a new one.

## AnchorPoint

`UIObject.AnchorPoint` is which point of **this object** `Position` places, as a
fraction of its own size. `(0, 0)` is its top-left corner and `(0.5, 0.5)` its
middle — which is what makes centring one property rather than an arithmetic
apology:

```luau
menu.Position = UDim2.fromScale(0.5, 0.5)
menu.AnchorPoint = Vector2.new(0.5, 0.5)
```

It is subtracted **after** the object's own size is known, so it never changes
the size — only the placement. It is a fraction per axis and is not restricted
to 0, 0.5 and 1; `(0.3, 0.7)` is ordinary.

## What "the parent" means

The rectangle a child lays out against is **not** its parent's `AbsoluteSize`.
It is the parent's *content rect*: the parent's rectangle after `UIPadding` has
been subtracted, and after a `ScrollFrame` has substituted its canvas.

For a direct child of a `ScreenGui`, the rectangle is the window — inset by
`UIService.SafeAreaInsets` when `ScreenGui.ScreenInsets` is on.

## AbsolutePosition and AbsoluteSize

Both are **outputs**, in window pixels, and both are read-only — writing one
would be arguing with the layout rather than changing it.

They are valid after the layout that produced them, which is the frame the
property was read in. Layout runs once per rendered frame, and only for a
`ScreenGui` something marked dirty. Two consequences:

- An element created this frame has whatever the last layout wrote, which for a
  brand-new element is zero, until the next frame.
- **An invisible element is skipped entirely**, so its absolutes are stale
  rather than updated.

`AbsolutePosition` is always the unrotated top-left: `UIObject.Rotation` affects
drawing and neither layout nor hit-testing.

## UIPadding

Four `UDim`s, one per side, on a modifier parented beside what it pads.
`PaddingTop` and `PaddingBottom` resolve their scale against the parent's own
**height**; `PaddingLeft` and `PaddingRight` against its **width**.

Padding participates in two opposite directions, and this is the subtlety worth
holding: on the way **down** it is subtracted, shrinking the rectangle offered
to children; on the way **up**, when `AutomaticSize` is measuring, it is added,
because padding is around the content.

## UIListLayout

A modifier that stacks its parent's `UIObject` children in a line.

```luau
--!strict
local layout = Instance.new("UIListLayout")
layout.FillDirection = Enum.FillDirection.Vertical
layout.Padding = UDim.new(0, 12)
layout.HorizontalAlignment = Enum.HorizontalAlignment.Center
layout.VerticalAlignment = Enum.VerticalAlignment.Center
layout.SortOrder = Enum.SortOrder.LayoutOrder
layout.Parent = menu
```

**A child laid out by one keeps its `Size` and loses its `Position` and
`AnchorPoint`.** The layout decides where each one goes, which is what a layout
is.

`UIListLayout.Padding` is the gap **between** children, not around them — that
is `UIPadding`'s job — and its scale resolves against the main axis.

**Which alignment is which depends on the fill direction.** On a horizontal
fill, `HorizontalAlignment` is the main-axis alignment and `VerticalAlignment`
the cross-axis; on a vertical fill they swap.

Ordering: `SortOrder.LayoutOrder` sorts by `UIObject.LayoutOrder` and the sort
is **stable**, so ties keep document order. `SortOrder.Name` sorts by name, by
code point — which survives the children being rebuilt in a different order.
Non-`UIObject` siblings, including the layout itself, are skipped.

Every child's size is measured **before** any placement, so the cross-axis
alignment has a total to work from. A stack that aligned as it went could not
centre itself.

With `UIListLayout.Wraps` on, a line that runs out of room breaks into a second,
and the cross-axis alignment is then against **the line's own band** rather than
the whole container — centring each line against the container would put every
line on top of every other. The first child of a line never wraps.

## AutomaticSize

`UIObject.AutomaticSize` replaces the resolved pixel size of an axis with the
size of the contents:

1. The `Size`-derived rectangle is computed first, on **both** axes.
2. The content is measured — text metrics, plus the children's extents (summed
   along the fill axis if there is a list layout, otherwise the furthest any
   child reaches, position and size together).
3. `UIPadding` is added on both sides.
4. The chosen axes are replaced.

So `AutomaticSize` overrides both halves of `Size` on that axis — but `Size` on
that axis is still what children and text wrapping are measured *against*. A
label with `AutomaticSize.X` and `TextWrapped` still wraps at the width its
`Size` gives.

## ScrollFrame

A `Frame` whose contents can be larger than it is. **It clips its descendants
whatever `ClipsDescendants` says** — a scrolling region that did not clip would
not be one — and an item scrolled out of view does not answer a click.

`ScrollFrame.CanvasSize` is a `UDim2` whose scale is relative to the frame's own
size, so `UDim2.new(1, 0, 3, 0)` is "as wide as me, three times as tall".
Children lay out against the canvas rather than against the frame.

> **Set both axes.** A `CanvasSize` of `UDim2.new(0, 0, 0, 800)` gives a content
> rectangle zero pixels wide, and children with an x scale collapse. Write
> `UDim2.new(1, 0, 0, 800)`.

`ScrollFrame.CanvasPosition` is in pixels and is clamped to what the canvas
allows at the next layout, so a write past the end settles at the end rather
than showing emptiness.

`ScrollFrame.ScrollBarThickness` defaults to 12; zero draws no bar and still
scrolls, which is what a touch surface wants. A bar appears only on an axis that
can actually move.

## UICorner

Rounds its parent's corners. `UICorner.CornerRadius` is a `UDim` whose scale is
a fraction of the parent's **shorter side**, so a pill is `UDim.new(0.5, 0)`
whatever the box's proportions.

It changes the drawing and not the layout or the hit test: a rounded button is
still a rectangle to the solver and to the pointer.

## What is not here

No `UIGridLayout`, `UIScale`, `UIStroke`, `UIGradient` or
`UIAspectRatioConstraint`. Three modifiers is the set, and each of those is a
real feature rather than an afternoon. There are no borders either.

## Where to look next

- [The UI tree](manual:ui/tree)
- [Text and images](manual:ui/text-and-images)
- [`UIListLayout`](api:UIListLayout) · [`UDim2`](api:UDim2)
