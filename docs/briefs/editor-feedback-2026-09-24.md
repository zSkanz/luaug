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
- [x] **A model or stamp moves as far as the pointer.** Reported again after
      the arrow fix: "a light drag moved it a lot" -- on a model.
- [x] **A CharacterBody is drawn as the capsule it moves as**, not as a box.
- [x] **Autocomplete reads this file's own code**: `Snake.` offers what the
      file put in `Snake`, `self.` in a method offers the instance's fields,
      and a value typed `: T` offers `T`'s fields.
- [x] **The editing keys every code editor shares** (word moves and deletes,
      whole-line cut/copy/delete/duplicate, indent/outdent, select line, select
      next match, jump to bracket, block comment).
- [x] **Text UI has a TextTransparency.**
- [x] **`Material` is a known global** to the lint and to completion.
- [x] **Play brings the viewport to the front.**
- [x] **The ribbons tabs have icons.** (Centring was tried and reverted at the owners word: it looked better on the left.)
- [x] **An index is a step in completion**: `Snake.Body[1].` offers what a
      `BasePart` has.
- [x] **A colour written in code has a swatch and the Properties picker**,
      which writes the value back in the form it was written in.
- [x] **A text's properties sit under Text**, `TextTransparency` included.
- [x] **Text alignment is `TextXAlignment` / `TextYAlignment`** (the owner's
      call, reversing a recorded divergence); old scenes read the old names.
- [x] **The selection is the part's own outline**, not a box standing off it.
- [x] **A part moved further than its own size in one tick is drawn where it
      landed**, not slid there (the owner's snake).
- [x] **No cross of lines through the view at the scene camera.** It was that
      camera's marker, seen from inside.
- [x] **A CFrame's rotation is editable in Properties**, as the three angles
      `Orientation` uses, beside its position -- it was nine read-only numbers.
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
- [x] **A ribbon of tabs across the top of the editor** (Home, Model, Test,
      View) that groups the tools by task -- under the menu bar and always
      there, not inside the viewport, which is only the world now.
- [x] **A Camera draws its view volume as a wireframe** when selected.
- [x] **Inserting or selecting an instance leaves a terrain brush.** A part added
      while sculpting stayed under the brush instead of being selected.
- [x] **Opening the terrain tools clears the selection**, so a brush and a
      selected part never compete for the same click.
- [x] **Interface scale in Preferences** behaves oddly -- find out what it does
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

- [x] **A new script opens with the caret on line 1**, visible and blinking.
- [x] **Hover and completion boxes contrast with the code** behind them, and
      their text wraps inside the box instead of running past it.
- [x] **One suggestion per name.** `print` showed twice, "in this file" and
      "global", in a file that never declared it.
- [x] **A suggestion can be clicked**, and **Enter accepts one**.
- [x] **No completion popup without a caret** in the editor.
- [x] **`print"olá"` does not confuse completion.**
- [x] **Pairs close themselves**: `(`, `[`, `{`, `"` and `'` insert their
      closer; typing the closer steps over it; a quote or bracket typed over a
      selection wraps it.
- [x] **Space between the line numbers and the code.**
- [x] **One Ctrl+Z undoes one action.** Some needed two.
- [x] **An error says what is wrong** at the end of its line, not only with a
      red underline.
- [x] **Ctrl+/ comments and uncomments** the line or every line selected.
- [x] **Type annotations are coloured** as the Luau grammar reads them: the
      names after `:`, `->` and `type X =`, generics, and `typeof`.
- [x] **Renaming a script renames its tab**, in the Explorer or anywhere else.
- [x] **A colour's swatch appears beside the colour under the pointer**, after
      its closing parenthesis, and a click on it opens the picker.
- [x] **`continue` is coloured as a keyword**, as `if` and `return` are.
- [x] **Signature help**: the parameters of the function being called, with the
      one being typed highlighted, and its doc (ADR 0093).
- [x] **Types across `require`**: a `ModuleScript` that defines `Snake` gives
      its type to the script that requires it, through `script.Parent`,
      `GetService` and `WaitForChild` walks and the locals holding them.
- [x] **`Signal` is typed** in completion, made by a script or an event.
- [x] **After `::` a module's name offers its types**, not its functions.
- [x] **Type errors are underlined**, beside the parse errors -- an undeclared
      type in an annotation included.
- [x] **An unsaved script's tab shows a floppy**, not a dot.
- [x] **Ctrl+F is a find and replace box** at the pane's corner: replace one
      or all, match case, whole word, regular expression, every match
      highlighted, Enter and Shift+Enter to step.
- [x] **The automatic `end`**: Enter after `then`, `do`, `repeat` or a
      function's `)` writes the closer below -- `end)` for a function passed
      to a call -- when the document has none for it.
- [x] **Saving the scene clears every scene script's floppy**, not only the
      tab that asked; a stamp's scripts likewise.
- [x] **`Signal`, `Collector` and `Promise` are native** (ADR 0094): `Signal`
      audited against GoodSignal and given `DisconnectAll`; `Collector` on
      Janitor's surface, newest first; `Promise` on evaera's semantics with
      `Enum.PromiseState` and `ExpectAsync`. The last two are Luau compiled at
      build time, so the shipping profile runs them too.

## Console

- [x] **The console's layout stays inside its window.**
- [x] **Its text can be selected and copied**: drag, Shift+click, double-click
      for a line, Ctrl+A, Ctrl+C and a right-click menu.
- [x] **Up and down walk the commands already typed**, as a shell does.
- [x] **Copy all**, for pasting the console into a report.

## Explorer, second report

- [x] **Any instance takes a child from the plus**, and the scene keeps what is
      put inside any service (the owner: "whether it does anything is another
      story").
- [x] **A script lives in the instance it is put in** (ADR 0092): made or
      dropped anywhere, `ScriptService` included, it is saved with the scene,
      and a script -- one read from a file included -- holds children, so a
      `Loader` keeps its modules.
- [x] **An interface element starts 50 by 50 pixels.**
- [x] **A selected interface element has handles**: eight to resize, the
      body to move, `AnchorPoint` respected, one undo step per drag.
- [x] **The starter is a scene with its scripts in it**: a script inside the
      part it turns, a module in `ReplicatedStorage`, one in `ScriptService`;
      the external-tooling files it carried and never used are gone.
- [x] **Left Shift slows the viewport's camera** to a quarter, for precision.

## Lighting

- [x] **Night is night.** From 06:00 to 18:00 the sun as before; warm at
      sunrise and sunset only, cooling through the blue hour, and a dark blue
      night lit by a cold moon opposite the sun, which casts the shadows.

## Sound

- [x] **`TimeLength` is in Properties**, and `TimePosition` stays inside it.
- [x] **A sound plays again.** `Play` starts from the start, `Resume` carries on
      and a sound that ends rewinds.

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
8. **"Enter does not accept" was the list coming back.** The accepted word
   matched itself, so the list reopened under the caret and the next Enter
   accepted it again instead of breaking the line. An accept no longer offers
   the list, and a row that is exactly what is typed is not offered at all.
9. **Two undo steps were an erase and an insert.** Typing, pasting or
   indenting over a selection erased it and then inserted, which the
   document recorded as two steps; it is one `replace` now.
10. **The interface scale ran away under the hand dragging it.** Every step
    of the drag rescaled the dialog and the slider, so the value under a still
    pointer changed. The scale is applied on release, with one-click presets.
11. **The scene kept three services and the Explorer let you fill all of
    them.** A `ScreenGui` made in `UIService` was lost at the next save. Every
    service's contents are saved now except the world (the file's root) and
    `ScriptService` (files); a `Player` is never written, because the engine
    makes those. A scene with nothing in them is byte-for-byte what it was.
12. **Types have no lexeme.** Luau's lexer reads `number` as a `Name`; the
    highlighter marks names where the grammar puts a type -- after a binding's
    `:`, after `::` and `->`, on the right of `type X =`, and in a generic
    list -- one line at a time, like the lexer it follows.
13. **The model drag summed itself once per frame.** A model has no
    transform, so a drag writes its parts; each frame applied the whole delta
    since the press to where the parts ALREADY were, so a four-metre drag over
    thirty frames put the model at sixty-two (the test that found it). The
    arrow fix was real and separate -- a part never showed this, because a
    part's drag writes an absolute frame. The parts' starting frames are kept
    with the drag now.
14. **A CharacterBody has no Shape**, being a `BasePart` and not a `Part`, so
    the renderer drew the default block around a capsule the physics swept.
    The renderer asks what a part is drawn as (`drawnShape`).
15. **`streaming_soak` is intermittent on the Linux tier**, as the Orbit
    shell's QA had already recorded: one run of this pass saw the circuit come
    back to a place with 1691 instances where the first visit had 1552, and
    the same tree passed on the next run and in every gate before and after.
    Recorded as found, not fixed; it is a streaming question, not an editor one.
16. **Completion knew the engine and not the file.** A table the file built,
    an instance of it, `self` in a method and a type the file wrote were all
    invisible to it, because the one reader of the file's own code answered
    `require` and nothing else. `sourceMembersOf` reads the same AST for those
    shapes -- a pattern reader, not Luau's type checker, which ADR 0057 keeps
    out of the build -- and hands a path that ends on an engine class to
    reflection. Where it cannot follow a value it says so and the old answer
    stands.
17. **The "grid on the camera" was the camera's own marker.** The editor
    starts at the scene's `CurrentCamera`, inside the wire sphere drawn round
    every camera so it can be seen and clicked; from inside, a sphere is two
    lines across the view. A ray from inside also passed within its radius
    whatever it was aimed at, so a click could select the camera instead of
    the part behind. A marker the eye is inside is now neither drawn nor
    picked (`eyeInsideMarker`).
18. **The selection had two marks, and one stood off the part.** The renderer
    outlines a selected part along its silhouette; the E1 wire box was still
    drawn over it with a margin of 1% of the part's size -- metres, on a large
    one. The box is drawn now only on the debug path, which has no outline.
19. **The snake was the engine's, not the script's.** Its tail is moved to the
    front of its head with one `CFrame` write, which is a teleport; the render
    interpolation (D047) drew every frame between two ticks at a point between
    where the tail was and where it went, so it slid through the body. A move
    longer than the part's largest side (and at least half a metre) is now
    drawn where it landed; a glide is interpolated as before. `streaming_soak`
    was also run five times on Windows and passed every time: its
    intermittence is on the Linux tier only.
20. **The scene knew the mount by where it was.** Every `Script` under
    `ScriptService` was taken for a file's, so one somebody made there was not
    saved, and nothing inside a file's script could be. The mount now marks
    what it made (`World::mounted`), and the scene writes a mark for a marked
    node that holds something authored (ADR 0092).
21. **A sound played once.** Its timeline stopped AT its length and `Play`
    carried on from `TimePosition`, so the next `Play` began at the end and
    ended on its first tick. The audio soak's own comment described the
    workaround -- rewind first -- as how a game has to write it. `Play` now
    starts from the start and a sound that ends rewinds.
22. **Completion stopped at patterns.** The file-local AST reader could not
    follow a `require`, a generic or a function's return, and every case the
    owner raised was one of those. Luau's own checker does all of it (ADR
    0093). Its new solver at this pin cannot generalize the definitions'
    `Instance.new` -- measured, not a budget and not a flag -- so the editor
    uses the old solver until the pin moves.
23. **The world is a keystroke behind the buffer.** The pane's writes to
    `Source` land at the next frame's safe point, so a checker handed the
    world's text never saw the call being typed. The asking tab's snapshot
    carries its buffer.
24. **Every night hour was a sunset.** Below the horizon the sun's colour
    stopped at its lowest-sun orange, and the horizon and the glow were tinted
    with it; auto-exposure lifted that dim orange until the scene was orange.
    The day strip (`scripts/daystrip.ps1`) showed it at 21:00, 00:00 and
    03:00. The warmth now fades from the horizon to nautical twilight, and the
    light hands over to a moon at eight percent of the sun from civil
    twilight, where neither is lighting anything.
25. **A built game lost its scene.** `luaug build` ships `content/` as a pack,
    and the host found the boot scene only as a loose file -- harmless while
    the scripts lived in `src/scripts`, fatal once they live in the scene. The
    scene and stamps are read from the pack when there is no file.
