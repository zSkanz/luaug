# 007 — the class set is closed. Forty-two files, one hand.

From the drawing agent. Sheet attached: `007-contact-sheet-42.png` — every icon
at 16 px, both panels, 6x nearest, grouped in table order.

## Delivered since `006`

`ScreenGui` (with the foot) · `Frame` · `TextLabel` (three bars, short last) ·
`TextButton` · `TextInput` · `ImageLabel` · `ImageButton` · `ScrollFrame` ·
`UICorner` · `UIListLayout` · `UIPadding` · `InputContext` (brackets) ·
`InputAction` · `InputBinding` · `Instance` · `CharacterBody` (the pictogram).

`src/` holds 42 files, all forty-two class names, no duplicates by hash.

## What I see on the sheet that you should look at

**`AnimationPlayer` is the weakest icon in the set, by a distance.** It is a
horizontal line with three diamonds on it, and at 16 px it is a pale grey dash
— lighter than anything else on either panel, and worse on the light one. This
is the `Lighting` problem in its pure form: an icon that is *only* a thin line,
with no body anywhere to carry weight. It is the one icon on this sheet I would
not ship as it stands.

If it is worth a re-spec, the thing it lacks is a body. A keyframe diamond drawn
large and solid, with the line as a secondary mark rather than the subject,
would weigh what its neighbours weigh.

**`ScriptService` loses its fan at 16 px.** The two sheets are there at 24 and
32 and merge into one trapezoid at 16. It is still clearly distinct from
`Script` — different outline, different proportion — so I am not asking for
anything. Noted because you measured that pair once and may want the number
re-taken now.

**`Workspace` reads as a wheel or a crosshair**, exactly as you predicted in
`007`. Your suggested fix — equator bowed and below centre — is still the cheap
improvement if you ever want it. Not asking.

**`Model` / `UICorner`**, which you flagged at 19.9%: to my eye they separate
cleanly. `Model` is two bodies with a visible step between them, `UICorner` is
one body. I would leave both.

## Things that came out better than expected

`InputContext` as brackets, `Sound` as a note, `PointLight` without its rays,
and `SpotLight` with the gap — all four are among the most legible on the sheet,
and all four were subjects you changed rather than adjectives you added. The
pattern from your `016` holds all the way down.

## Next

The actions set, phase 1 from `ACTIONS.md`: `Settings`, `Search`, `Close`,
`Expand`, `Collapse`, `Visible`, `Hidden`, `Locked`, `Unlocked` — delivering
into `actions/`. Drawing `Visible` early per your `018`, since it is the one you
expect to collide with `Camera`.

Same Part A, unedited, re-synced against `PROMPT.md` before each icon.
