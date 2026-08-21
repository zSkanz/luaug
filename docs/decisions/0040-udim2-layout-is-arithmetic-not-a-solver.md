# 0040 — UDim2 layout is arithmetic, not a constraint problem; v1 does not call Clay

- Status: accepted
- Date: 2026-08-20
- Amends: ADR 0011 (its Clay clause only; the ImGui and stb_truetype clauses stand)

## Context
ADR 0011 chose Clay as the layout solver behind the in-game UI, on the premise
that "writing a layout solver from scratch is expensive". `architecture.md` §2
restates it: "UDim2 + list layouts + AutomaticSize compile to Clay configs".
Clay was vendored at M6 (`v0.14`) on the strength of that decision.

Building the UI found that the premise does not hold for this model, and the
reason is specific rather than a matter of taste.

**A `UDim2` placement is not a layout question.** A child's rectangle is

    absSize = parentSize * Size.Scale + Size.Offset
    absPos  = parentPos + parentSize * Position.Scale + Position.Offset
              - AnchorPoint * absSize

Two multiplies and an add, per axis, in one top-down pass. There is nothing to
solve: no constraint is under-determined, no sibling affects another, and no
iteration converges. Clay solves **flow** layout — FIT/GROW/PERCENT sizing with
padding and gaps — which is a different problem, and the only parts of our model
that are that problem are `UIListLayout` and `AutomaticSize`.

**And Clay cannot express what §2.2 documents**, which is what settles it:

- `UDim.Scale` is deliberately unclamped (a 1.5 scale is a legal, meaningful
  overhang, and the conformance suite pins it). Clay's `CLAY_SIZING_PERCENT`
  expects 0–1 and raises `CLAY_ERROR_TYPE_PERCENTAGE_OVER_1` outside it
  (`clay.h:294`, `:788`).
- `AnchorPoint` is a fraction of the element's own size on each axis, so
  `(0.3, 0.7)` is ordinary. Clay's floating attachment — the only way to place
  an element without it affecting its siblings — takes corner and centre
  *enumerators*, not fractions.
- Every absolutely-positioned element would therefore have to be a floating
  element, and in a Roblox-style UI that is most of them. A solver used only for
  the elements it cannot place is not a solver we are using.

Two smaller facts, recorded because they would have mattered anyway: Clay keeps
one current context in a file-scope global (`clay.h:1018`), so it is neither
reentrant nor thread-safe; and its arena is sized once up front, with exhaustion
arriving through an error handler.

## Decision
**`ui` computes the layout directly and does not call Clay in v1.** Roughly
three hundred lines, in two passes over each dirty `ScreenGui`: a bottom-up pass
that resolves `AutomaticSize` from text metrics and children's extents, and a
top-down pass that assigns every `AbsolutePosition` and `AbsoluteSize`.
`UIListLayout` is a single-axis stack with optional wrap and cross-axis
alignment inside that walk; `UIPadding` insets a content rect; `ScrollFrame`
offsets one.

ADR 0011's other two clauses are untouched: ImGui is still the debug UI, and
text is still stb_truetype with the same documented shaping gap.

**Clay stays vendored and pinned.** Removing a dependency is a human decision
(R5, MASTER_PROMPT §10), and this ADR does not make it — it records that v1's
layout does not call it. The question "remove the row, or keep it for a UI feature
that is genuinely flow-shaped" went to the human on 2026-08-21 and was answered:
**remove it.** A vendored dependency nobody calls still enters every build, every
notices file and every future reader's half hour, and "not used yet" and "does
not fit the model" are different states. `third_party/clay/`, its manifest row
and its notices row went with the answer, in the M6 session that owned the build.

## Consequences
The layout is testable as arithmetic, which is the main thing this buys: a case
can assert an exact rectangle for an exact set of properties, and a disagreement
has one place to be. It also removes the impedance layer — no arena to size, no
global context to set, no translation between two models, and no third-party
API that reshapes between releases (the reason ADR 0011's row carried no version
at all until M6 read one off upstream).

The cost is the code, and the honest accounting is that it is less code than the
translation would have been. The risk it takes on is that a future UI feature
IS flow-shaped — a grid, a flexbox-style `UIListLayout` growth model — and has
to be written rather than configured. `UIGridLayout` is already outside v1
(M6 brief, NOT-in-scope 4). If one arrives, this ADR is what says Clay was
considered and why it did not fit, and re-vendoring is an ADR and a manifest row
rather than a rediscovery.

**What this ADR does not say:** that ADR 0011 was wrong to choose Clay. The
choice was made from a research report, before anything in this engine had a
`UDim2`, and it was the right shape of decision to make at that point. What
changed is that the model got written down in §2.2 with an unclamped scale and a
fractional anchor point, and those two lines are what Clay cannot express.
