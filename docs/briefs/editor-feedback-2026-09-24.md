# Editor feedback pass, 2026-09-24

The owner and a friend tested the editor from a built folder and wrote down
what got in their way. This ledger is that list, grouped by where it lives,
with what was done about each item. It builds on the Orbit shell
([`orbit-shell.md`](orbit-shell.md)), committed as the base for this pass.

Where an item says "the way users expect", the reference is the editors most
people coming to LuauG already have in their hands. The layout and key choices
are ours, written clean-room (R7).

Legend: `[ ]` not started · `[~]` in progress · `[x]` done.

## Viewport

- [x] **A drag on an arrow moves as far as the pointer, never to infinity.**
      Reported mid-pass: a small mouse movement sent the part away forever.
- [x] **WASD flies without a held mouse button.** While the viewport has focus
      and no text field does, W/A/S/D/Q/E move the camera; the right button
      still looks around.
- [x] **A shortcut never moves the camera.** With Ctrl, Alt or Shift+Ctrl held,
      the fly keys are shortcuts and not movement (Ctrl+D duplicated *and*
      slid the camera right).
- [x] **Tool keys that do not collide with flying**: Ctrl+1 select, Ctrl+2
      move, Ctrl+3 scale, Ctrl+4 rotate, Ctrl+L local/world. The single-letter
      tool keys that are fly keys go.
- [x] **Alt+click selects the part itself**, not the model it belongs to.
- [x] **Selecting a model highlights every part in it**, not only its box.
- [x] **Shift+P toggles a free camera while playing.**
- [x] **The snap increments live on the viewport's toolbar**, where they are
      used, and not only in Viewport Settings.
- [x] **A ribbon of tabs above the viewport** (Home, Model, Test, View) that
      groups the tools by task.
- [x] **A Camera draws its view volume as a wireframe** when selected.
- [x] **Inserting or selecting an instance leaves a terrain brush.** A part added
      while sculpting stayed under the brush instead of being selected.
- [x] **Opening the terrain tools clears the selection**, so a brush and a
      selected part never compete for the same click.
- [ ] **Interface scale in Preferences** behaves oddly -- find out what it does
      not respect and fix it.

## Explorer and Properties

- [x] **Properties reads as a grid**: collapsible categories, a label column and
      a value column with a shared divider, quiet row separators, checkboxes
      for booleans, and composite values (a CFrame's position and orientation)
      as expandable rows. Our theme, not a copy of anybody's.
- [x] **The Properties filter keeps the category** a match belongs to and
      matches anywhere in a name, not only at its start.
- [x] **Group and Ungroup for Folder**, as for Model.
- [x] **Dragging in the Explorer**: a Script dragged from Workspace to
      ScriptService is refused. Audit every reparent rule the drag
      applies and fix the ones that are wrong.

## Script editor

- [ ] **A new script opens with the caret on line 1**, visible and blinking.
- [ ] **Hover and completion boxes contrast with the code** behind them, and
      their text wraps inside the box instead of running past it.
- [ ] **One suggestion per name.** `print` showed twice, "in this file" and
      "global", in a file that never declared it.
- [ ] **A suggestion can be clicked**, and **Enter accepts one**.
- [ ] **No completion popup without a caret** in the editor.
- [ ] **`print"olá"` does not confuse completion.**
- [ ] **Pairs close themselves**: `(`, `[`, `{`, `"` and `'` insert their
      closer; typing the closer steps over it; a quote or bracket typed over a
      selection wraps it.
- [ ] **Space between the line numbers and the code.**
- [ ] **One Ctrl+Z undoes one action.** Some needed two.
- [ ] **An error says what is wrong** at the end of its line, not only with a
      red underline.
- [ ] **Ctrl+/ comments and uncomments** the line or every line selected.
- [ ] **Type annotations are coloured** as the Luau grammar reads them: the
      names after `:`, `->` and `type X =`, generics, and `typeof`.
- [ ] **Renaming a script renames its tab**, in the Explorer or anywhere else.

## Console

- [ ] **The console's layout stays inside its window.**

## Findings

1. **The arrow ran away because the solve had no horizon.** A translate arm
   took the point on its infinite line nearest the pointer's ray; for an arm
   receding from the camera, a pointer at or past its vanishing point is
   nearest a point kilometres away or behind the eye. A test reproduced it
   (1135 m behind the camera, one pixel from the horizon). The drag now solves
   through the plane that holds the arm and faces the camera, and refuses a
   grazing hit, one behind the camera, or one beyond fifty times the handle's
   distance -- the part stays where the last usable pixel put it. Plane
   handles and rotate rings take the same refusal.
2. **The single-letter tool keys had to go for WASD to fly without a button.**
   W was move and E rotate; both are fly keys now. Ctrl+1 to Ctrl+4 and
   Ctrl+L replace them, and Escape takes over Q's "back to selection".
3. **The detached view in play mode never flew.** The eye button (S5.8)
   switched the view to the editor's camera, but `driveCamera` returned early
   whenever the run state was not `Editing`, so the view was frozen where the
   editor left it. Shift+P and the eye now fly it, and the game gets no input
   while it is detached.
4. **The Explorer's drop rules allowed places the scene never saves.** The
   plus, paste and drop all accepted `ScriptService`, `Lighting` and the data
   model itself, and what went there was gone at the next save or play. And the
   edge bands of a row under another parent accepted a drop and did nothing.
5. **`ScriptService` is a mount, so a script dropped there becomes a file.**
   Its source is written to `src/scripts/<folders>/<name>.luau` and mounted at
   once. **Undo does not reach the disk**: it brings the scene's copy back and
   leaves the file, so both then exist and the file has to be deleted by hand.
   A mounted script cannot be dragged back out, because the file would mount it
   again at the next open and both would run.
6. **The Properties headings were the declaring classes.** A part's colour,
   size and collision sat under `BasePart`, `PVInstance` and `Instance`. They
   are grouped by task now through a table in the editor (`propertyCategory`),
   not a field in the IDL: it is presentation, and a property the table does
   not name falls under Behavior rather than going missing. The filter already
   matched inside names; what it lost was the heading, and several words.
7. **The ribbon's first-in-row test cannot ask the cursor.** The viewport's
   window has no padding, so "is the cursor past the padding" was true for
   every button and each one took a row of its own. A flag set per tab is the
   answer; found by capturing the window, not by a test.
