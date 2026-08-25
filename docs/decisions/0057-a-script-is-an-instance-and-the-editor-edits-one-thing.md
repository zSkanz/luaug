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

| The instance came from | Ctrl+S writes |
|---|---|
| a file under `src/scripts` | that `.luau` |
| the scene, or `Instance.new` in the editor | the open `.scene.json` |

The mapping already exists: `ModuleRegistry::Entry{path, source, instance}`
(`modules.cpp:458`).

**And Ctrl+S does nothing else, which is a correction to this ADR rather than a
detail of it.** The first version rebuilt the world after writing the file, so
that a running VM would pick the change up. Every symptom that followed was a
symptom of that: the panels vanished, the tabs closed, the caret jumped to line
one, the Explorer collapsed, and the screen flashed. Each was patched in turn,
and the patches were the tell — **saving a text file must not destroy a world.**

It never needed to. The pane writes `Source` on the instance as somebody types,
so the world already holds the new text; and
[ADR 0058](0058-a-script-runs-when-you-press-play.md) makes PLAY what starts a
script, compiling `Source` at that moment. In the editor there is no running
chunk to refresh. Ctrl+S is about the FILE surviving the editor being closed, and
that is a `writeTextFile`.

### 3. The editor does not reload, and that is the answer rather than the gap

`reloadWorld` is complete and unreachable from an editor session: `luaug edit`
never passes `--dev-control`, and the only call site is gated on it. This ADR
first proposed giving the editor its own path to it. **That was wrong**, and the
paragraph above says why: with play compiling `Source`, an editor session has
nothing to reload.

The hot-reload path stays exactly where it was, serving `luaug dev` — a game
running outside the editor, where a file change does have to reach a live VM.

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

**And the second source is the WORLD, which is the half a type checker would not
have had.** `Workspace.MainCamera` is not a fact about the `Workspace` class --
it is a fact about this project's tree, sitting in memory two panels away from
the tab being typed into. So a dotted path is walked instance by instance from
the DataModel, and what it resolves to offers its **children** beside its
members:

| What is typed | What answers |
|---|---|
| `Workspace.` , `game.Workspace.` , `Lighting.` | that instance's children, then its class's members |
| `script.Parent.` | the same, from the instance the tab is editing |
| `Workspace:WaitForChild("` , `.FindFirstChild("` | its children, by name, inside the quotes |
| `game:GetService("` | every class flagged `Service` |
| any other string | **nothing**, which is a state of its own |

The first segment is the only one with rules -- `game` is the root, `script` is
the tab's instance, and everything else is a child of the root, which is what a
service is. **There is no list of service names anywhere in this file**, and
that is the point: a service registered by a module this build has never heard
of resolves for free.

**A step may be a call that names something**, because otherwise the first line
of most Luau files ever written stops the walk dead:
`game:GetService("AudioService").` is a path through `AudioService`, and so is
`Workspace:WaitForChild("Level").`. Only a bare string literal counts -- a call
with an expression in it is a call this cannot read, and it stops rather than
guesses.

**And a step may be a local somebody assigned earlier.** `local RunService =
game:GetService("RunService")` is read the way a person scrolling up would read
it: one shape, `local NAME = <path>`, spliced in front of the path being
resolved, three hops deep. Nothing is evaluated, no function is followed, and no
expression is weighed -- which is the line ADR 0018 draws and the reason this
stays on the right side of it.

**A dot with nothing readable behind it offers nothing**, which is a state of
its own: `t:GetChildren().` used to fall through to the keyword list, and a list
of every reserved word in the language under a dot is never the answer.

**And Luau's own surface is offered too** -- `typeof`, `pcall`, `assert`,
`math.floor`, `string.format`, `table.create`, `buffer.readf32`, all of it.
That list lives in `engine/script/include/luaug/script/stdlib.h`, beside the
sandbox rather than beside the editor, because **it is a fact about the VM**:
the script module owns the sandbox, a test there can boot one, and `engine/app`
already depends on it.

**It is written down, and that needs an argument.**
`api/defs/libraries.api.luau` refuses to declare Luau's libraries in as many
words -- "re-declaring them would make this file a second source of truth for
something the pin already fixes" -- and the same is true here, with one
difference: the editor has to OFFER these names and cannot boot a VM to answer a
keystroke. So the drift is caught rather than prevented. `sandbox_tests.cpp`
stands a real sandboxed VM up and checks the list against it **in both
directions**: every name exists with the type it claims, and every key of every
library table is named. Bumping the Luau pin and gaining a function fails a test
instead of quietly leaving the editor a version behind.

**The list is this engine's surface, not stock Luau's.** `os` carries three
names because `removeUnsafeGlobals` takes `difftime` off; `getfenv`, `setfenv`,
`newproxy` and `loadstring` are not offered because they are not there. That is
the whole reason the list is checked against THIS sandbox rather than against
Luau's documentation.

Children are **one row per name**: six parts called `Ground` insert the same six
characters, and a member wins a collision because `workspace.Name` is the
property. A colon offers no children at all, because a child is not callable.

"Any other string offers nothing" is worth its row. Without it, typing a message
in quotes pops a list of every keyword in the language over it -- which is what
the first version did.

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

**A save is cheap, and the first design made it the most expensive thing in the
editor.** Writing a file is a syscall; rebuilding a world is destroying a VM, a
physics world, every instance and every panel pointer, and then putting six
kinds of editor state back by hand. The second was chosen because it looked like
the safe answer to "what if something is running", and the honest answer to that
question is that in the editor nothing is.

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
