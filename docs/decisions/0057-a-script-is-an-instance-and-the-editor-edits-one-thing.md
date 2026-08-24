# 0057 — A script is an instance, and the editor edits one thing

- Status: accepted
- Date: 2026-08-24
- Extends: 0046, 0050, 0056
- Completes: 0050

## Context

**You cannot write a line of Luau inside this engine.** Six editor milestones
built an Explorer, a Viewport, a Properties grid, a Content browser and a
Console, and none of them built a place to put code. The Properties panel draws
`Script.Source` through a `char buffer[256]`
(`engine/app/src/debug_overlay.cpp:1468-1491`) and refuses to edit anything
longer than 255 bytes — so the one property that carries a game's behaviour is
the one property the editor cannot touch.

**And the model underneath it is half-built.** ADR 0050 decided that a script's
source is a property of the instance. Two things it implies were never written:

- `engine/script/src/modules.cpp:454-458` — the mount creates a `Script`
  instance per file and keeps the text in `ModuleRegistry::Entry::source`. It
  never calls `setProperty("Source", …)`, so a mounted script's `Source` is the
  empty-string default.
- `engine/script/src/modules.cpp:468-522` — `startScripts` iterates
  `ModuleRegistry::entries`, the mounted-file list. Nothing anywhere walks the
  `ScriptComponent` pool, so a `Script` created in the Explorer or authored into
  a scene is saved with its `Source` and **never runs**.

Files that run without a `Source`, and instances with a `Source` that do not
run. ADR 0050's own text says the two "neither knows about the other", and that
turned out to be true of the code in a way the ADR did not intend. A script
editor built on that would have to lie about which of the two a tab was editing.

## Decision

**An instance is the only thing that runs, a tab edits its `Source`, and where
Ctrl+S sends the text is a property of where the instance came from.**

### 1. `startScripts` walks the world

Every enabled `Script` in the world starts, from its own `Source`, in
`World::collectDescendants` order — depth-first preorder, which
`world.h:379-380` already specifies as "the same document order the Find family
tie-breaks on". Document order rather than pool order is not a preference: pool
order is an allocation artefact, and R10 forbids iterating an unordered
container into observable order.

The mount narrows to what it should always have been: **read the files under
`src/scripts` and put their text into the instances it creates.**
`require(ModuleScript)` already reads `Source` (`modules.cpp:242`) and is
unchanged.

### 2. A tab edits `Source`; Ctrl+S persists to the origin

Every edit goes through the Inspector's pending-write path at the frame's safe
point, never a direct component write — ADR 0046's second consequence, unchanged.
What differs is where **Ctrl+S** puts it:

| The instance came from | Ctrl+S writes | Then |
|---|---|---|
| a file under `src/scripts` | that `.luau` | reloads the world |
| the scene, or `Instance.new` in the editor | the open `.scene.json` | nothing else |

The mapping already exists: `ModuleRegistry::Entry{path, source, instance}`
(`modules.cpp:458`).

### 3. The editor gets its own reload

`reloadWorld` is complete (`engine/app/include/luaug/app/reload.h`) and
unreachable from an editor session: `luaug edit` never passes `--dev-control`
(`tools/cli/commands/edit.luau:35-40`), and the only call site is gated on it
(`engine/app/src/engine.cpp:1438`). It becomes an `EditorCommands` field drained
at the same safe point as `play` and `save`, calling the same function.

### 4. Syntax colour is theme data, and the code face is Cousine

ADR 0056 left this open in as many words — "a syntax-highlighting palette for
the console or a future script editor, which is a different problem with a
different set of tokens". It is picked up on the same terms:

**Eight tokens** in a `SyntaxPalette` beside `ThemePalette` — `keyword`,
`string`, `number`, `comment`, `identifier`, `operatorToken`, `attribute`,
`errorToken` — one per class the Luau lexer distinguishes, defined for both
themes and held to the same 4.5:1 bar, asserted by the same `contrastRatio`.

**`third_party/imgui/misc/fonts/Cousine-Regular.ttf`**, staged as
`content/fonts/Mono.ttf`. Already vendored, SIL OFL 1.1 — the same licence as
Inter, which `tools/repo/licensecheck.luau:49` already admits. **Inter remains
the engine's typeface**; this is the code pane's face, and saying so here is what
keeps somebody from reading it as a reversal of the M7 decision. A code editor
set in a proportional face is not a code editor, and that is the whole argument.

The credit needs a mechanism rather than a hand edit: `THIRD_PARTY_NOTICES.md`
says "GENERATED FILE — do not edit" and `tools/repo/vendor.luau:588-598` emits
one row per manifest row from its `license` field alone, so a bundled asset under
a different licence than the row it lives in has nowhere to be declared. A
manifest row gains an optional `bundled` list and the generator gains a second
table.

### 5. Autocomplete comes from the engine's own reflection, not from Analysis

`Luau.Analysis` is not built, and that is written down rather than accidental:
`cmake/luaug_luau.cmake:21-40` records it as 35% of a cold build's compile time,
and ADR 0018 makes type checking `luau-analyze`, a tool. Linking it reverses
both.

`ClassRegistry` already carries every class, property, method and event with its
doc string, generated from `api/defs/*.api.luau`
(`class_registry.h:168-193`). Completion resolves the token before `.` or `:` to
a class and lists its members through the hierarchy, plus the identifiers the
file already contains and the keyword list. **It does not infer types across
expressions**, and that limit is stated here so nobody reads the absence as a
bug.

## Consequences

**The world hash changes**, exactly as ADR 0050's own did, because mounted
scripts now carry a non-empty hashed property. The determinism traces are
re-recorded on both tiers. A trace is the only thing that would catch a wrong
iteration order, which is why the order is specified above rather than left to
whatever the pool hands back.

**A `Script` you create in the editor runs.** That is the whole point, and it is
the behaviour every comparable engine has. It also means a project can put
behaviour in a prefab, which ADR 0049's stamps could already carry and nothing
could execute.

**Two sources for one world gains a third shape.** `world_host.cpp:384-400`
already warns when a scene and a script both populate `Workspace` (D074); a
scene that authors a `Script` whose file also exists under `src/scripts` is the
same class of mistake and gets the same treatment — said out loud, not refused.

**A code pane is drawn by hand**, because `InputTextMultiline` cannot carry
per-token colour, has no length guard on its multiline path
(`imgui_widgets.cpp:5512-5515`), and keeps exactly one `ImGuiInputTextState` for
the whole context (`imgui_internal.h:1273`) — so N open documents cannot each
keep a cursor. Vendoring a third-party editor widget would be a new dependency
under R5, and none of them know Luau, while `Luau.Ast` is already linked into
every profile that has an editor.

**The debugger costs less than it looks.** The VM has had `lua_breakpoint`,
`lua_singlestep` and the `lua_Callbacks` debug hooks since it was vendored
(`lua.h:555-559`, `:609-612`), scripts are already compiled with
`debugLevel = 2`, and `luaG_breakpoint` recurses into every nested proto
(`ldebug.cpp:427-430`) — so one call on a chunk's top-level function reaches
every line in the file, including closures that do not exist yet. A hit
breakpoint parks the coroutine with `LUA_BREAK`, which `signals.cpp:507-513`
already anticipates in as many words, and the frame loop keeps drawing.

**What this does not decide.** Whether `src/scripts` eventually goes away —
ADR 0050 left that open and it stays open. Type inference. A protocol for an
external debugger. Multi-cursor editing, folding, and a minimap: the pane is
built so they are additions rather than rewrites, and none of them is promised.
