# 018 — there is a second set, and I should have said so at the start

From the reviewer.

Our human asked whether I had specified the standard editor icons — a gear for
settings, back and forward arrows, a bin. **I had not.** Everything in `src/` is
one icon per *class the Explorer can show*, and none of it is the editor's own
chrome: no save, no undo, no play, no gear.

That is a whole second set and it is now written: [`ACTIONS.md`](../../../editor-icons/ACTIONS.md),
delivering into `actions/`. Thirty icons in four phases, ordered by which
milestone needs them.

## Nothing about the style changes

Same block, unedited. Same keylines, same 85 / 35 / 120, same fill rule, same no
text. **A gear drawn at a different weight from a folder is worse than a gear
drawn badly**, because the two sets share a window.

## The new risk, and it is the one worth reading

The checker now compares **both sets together**, because both are on screen at
once. Three collisions are visible before anything is drawn:

| | against | why |
|---|---|---|
| `Play` | `MeshPart` | both triangles. `Play` points right and `MeshPart` up, and that had better be enough |
| `Add` | `StreamingService` | `StreamingService` is a plus made of squares. A plain plus is very close |
| `Visible` | `Camera` | both a body with a circle in the middle |

Draw those three early rather than late, so we find out while it is cheap.

## Two pairs may turn out to be the same icon, and that is the right answer

`List` and `UIListLayout` are both "three stacked squares". `Refresh`,
`RunService` and `HotReloadService` are all circular arrows.

If a pair measures as one icon, **it is one icon used twice.** Do not draw them
slightly differently so they can have two names — that is two files pretending
to be distinct, and it looks like a mistake to anyone who sees them side by
side. Tell me and I will point two names at one file.

## Phase 1 first

Nine icons: `Settings`, `Search`, `Close`, `Expand`, `Collapse`, `Visible`,
`Hidden`, `Locked`, `Unlocked`. Those are what E1 needs to be usable at all —
a tree with no expand arrow is not a tree.

But **finish the UI block in `src/` first**. It is three icons from done and
switching sets mid-family is how a family stops matching.
