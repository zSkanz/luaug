# E2 Kickoff — Moving Things

- Started: 2026-08-22
- Roadmap section: docs/roadmap.md#e2--moving-things-l
- Phase: post-v1 phase 1 (the visual editor), milestone 2 of 4

**Read E1's brief before this one**, and its first paragraph in particular. E1
was opened as "the editor opens" and closed as an editor, absorbing most of what
the first cut called E2 — the loop, the content browser, the menu bar, undo, and
a scene format. What is left here is what E1 could not have done first, and
nothing on it was discovered late.

## Goal (restated)

Make the editor a thing you move objects with. Today a transform is changed by
typing a number into the properties grid, one instance at a time, and the only
way to bring an instance into the world is to write `Instance.new` in a script.
By the end of this milestone somebody selects three parts in the viewport, drags
them a metre with a handle, presses ctrl-Z once and gets all three back;
right-clicks in the Explorer and makes a `Part` that appears in front of them;
and drags it onto another instance to reparent it. Nothing more — this is direct
manipulation over the loop E1 built, and every large thing next to it (prefabs,
importers, distributing the editor) belongs to a later milestone.

## Scope checklist (from roadmap)

- [ ] Translate, rotate and scale manipulators in the viewport, world and local space
- [ ] Snapping, with a modifier to suspend it
- [ ] Multi-select: ctrl-click and shift-range in the Explorer, ctrl-click in the viewport
- [ ] Properties over a multi-selection: common properties, and a mixed value marked as mixed
- [ ] Creating an instance from the Explorer context menu and the menu bar
- [ ] Reparenting by drag in the Explorer, with a drop-between-rows target
- [ ] Batch delete and duplicate, one undo step each
- [ ] The gesture-based undo key, extracted out of `engine.cpp` and tested
- [ ] `worldToViewport` in `picking.h`, as the exact inverse of `rayThroughPixel`
- [ ] The gizmo and the selection outline submitted camera-relative
- [ ] An `authorable` predicate covering engine-owned, `generated`, and the ancestors of both

## NOT in scope

Prefabs and nested scenes (E3). An asset importer (E3). Distributing the editor
(E4). Shape-exact picking. Solid gizmo geometry — `DebugDraw` is a line list and
stays one. A second viewport, an orthographic view, view bookmarks. Align and
distribute. Copy and paste. A dedicated transform panel. Scripting the editor.
And the Luau VM that a stop does not put back, which is still E1's honest limit.

## What four reconnaissance passes found, before a line was written

Five passes sized E1 (ADR 0046); four sized this one, and they are the reason
the roadmap section has six decisions in it rather than a task list.

**The foundations are there and they are tested.** `rayThroughPixel` reads its
two tangents back off the projection matrix the frame was rendered with, so a
gizmo built on it cannot disagree with the image. `DebugDraw` draws in a pass
with no depth attachment, so it is already over everything. `World::setParent`
already returns a typed error and already refuses `newParent == id` and
`isAncestorOf(id, newParent)`. `World::clone` already rewires references that
point inside the copied subtree. ImGui 1.92.9b-docking has drag-and-drop and
`BeginMultiSelect` compiled in.

**Three things are broken today and only building on them would have found it.**

1. **The undo coalescing key cannot survive a manipulator.** It is computed in
   `engine.cpp` as `(target << 32) | property`, and only when the frame has
   exactly one pending write — two or more give zero, and zero never coalesces.
   A gizmo writes `CFrame` and `Size` together, or writes to three selected
   parts, on the first frame of the first drag. Nothing tests the calculation:
   `editor_tests.cpp` drives `record` directly and never sees it.
2. **The selection outline shakes far from the origin, and nobody has reported
   it.** `submitSelection` submits in world coordinates and `DebugDraw::rebaseTo`
   subtracts in f32, so the absolute metre value is quantised before the camera
   is taken off it. Half a millimetre at four kilometres, worse beyond. A gizmo
   built the same way inherits it onto a handle somebody is dragging precisely.
3. **`isEngineOwned` does not know about `generated`.** Drop an authored part
   inside a streamed chunk's folder and the scene format skips it — the
   serializer skips a generated subtree whole — and the next eviction destroys
   it silently. Nothing stops that today; nothing has tried.

**And one absence worth naming**: there is no world-to-screen projection anywhere
in the repository. `worldToViewport` is new, and it belongs beside
`rayThroughPixel` and tested by round-trip against it.

## Subagent plan

The interfaces freeze first, in this order, and every one of them is orchestrator
work because each touches two modules' seams:

1. `Inspector`'s selection set and `pruneDead` — everything else reads it.
2. The gesture, and the extracted coalescing key.
3. `picking.h`: `worldToViewport`, the gizmo frame, the axis hit test, the drag
   solve. Pure functions, no ImGui, no `World`.
4. `Editor`: `createInstance`, `reparent`, the batch forms, and `authorable`.

Fanned out only after those headers compile:

- **Test authors** — `picking_tests.cpp` cases from the gate wording alone, not
  from the implementation. The corner and near-parallel cases are the ones that
  matter and they are stated in the gate for that reason.
- **An adversarial reviewer** on the diff, briefed at R10 (a gizmo must not reach
  simulation state), R3 (the editor draws literals; ADR 0046 §"R3 does not apply"
  — but a *game*'s strings still obey it), R17, and the f64/f32 boundary, citing
  rule numbers.
- **A research verifier** for the ImGui multi-select-with-clipper contract, which
  needs the `RangeSrcItem` seek and which `drawExplorer`'s clipper loop has none
  of. Answers quoted from `third_party/imgui`, file and line.

Not fanned out: the selection refactor, the undo change, and anything that
touches `engine.cpp`'s frame loop.

## Gate checklist (verbatim from roadmap)

- [ ] `luaug edit examples/06-scene` opens, a part is selected, and each of the three
      manipulators moves it in the viewport. A screenshot per mode is attached to the
      gate record.
- [ ] **The manipulator arithmetic has unit tests over a camera and a viewport
      rectangle**, covering: an axis handle hit at the centre of the screen and at a
      corner; an axis nearly parallel to the view direction; a drag that begins off
      the axis; a non-square viewport; and a round trip — `worldToViewport` of a
      point, back through `rayThroughPixel`, aiming at that point — checked at the
      four corners, because that is the aspect-ratio error the file already exists to
      catch.
- [ ] **One drag is one undo step**, proven headlessly: sixty frames of writes inside
      one gesture produce one history entry, and two gestures over the same property
      produce two. The extracted key has its own test; the inline calculation it
      replaces had none.
- [ ] **A transform over a multi-selection moves each instance by the same delta**,
      proven on a selection whose members start at different transforms: three parts
      a metre apart are still a metre apart after the drag.
- [ ] **The gizmo does not shake four kilometres from the origin.** The vertices
      `DebugDraw` holds for a gizmo submitted at 4 km agree with the exact
      camera-relative value to within a tenth of a millimetre. The same check covers
      the selection outline, which is the defect it finds.
- [ ] Multi-select in the Explorer and in the viewport; the Properties panel shows
      the common properties of a mixed-class selection and marks a differing value as
      mixed. Proven by tests over the free functions that compute both, since the
      panel itself cannot be driven headlessly.
- [ ] Creating an instance from the UI lands under the parent the menu was opened on,
      is selected, and is taken back by one undo. Reparenting by drag moves a
      subtree, refuses a cycle, and refuses a target inside a streamed chunk — all of
      it driven through `Editor` directly by a test rather than by a mouse.
- [ ] Deleting a selection of four is one undo step, and undoing it brings all four
      back with the same instance ids, which is the property E1's delete test already
      asserts for one.
- [ ] `scripts/localgate.ps1` green on every stage; `luaug check` clean; docs-lint
      clean.
- [ ] **A human opens the editor on the flagship, moves something, and says whether
      it moves the way a manipulator should** — deliberately not automatable, and the
      gate item every milestone since M4 has proven is where the real defects come
      from.

## Attempted / abandoned

_(appended during the milestone; MASTER_PROMPT.md §12)_

## Gate Record

_(filled at milestone end, before human review)_
