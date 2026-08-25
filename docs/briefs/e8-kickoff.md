# E8 Kickoff — The Script Editor

- Started: 2026-08-24
- Roadmap section: [docs/roadmap.md § E8](../roadmap.md#e8--the-script-editor-xl)
- ADR: [0057 — A script is an instance, and the editor edits one thing](../decisions/0057-a-script-is-an-instance-and-the-editor-edits-one-thing.md)

## Goal (restated)

Six editor milestones built an Explorer, a Viewport, a Properties grid, a
Content browser and a Console, and none of them built a place to put code. The
Properties panel drew `Script.Source` through a 256-byte buffer and refused to
edit anything longer, so the one property carrying a game's behaviour was the
one property the editor could not touch.

The bar was given as Roblox Studio and the debugger was named. So was the shape:
several scripts open at once, as tabs where the Viewport is, draggable out to sit
beside the world.

## Scope checklist (from roadmap)

- [x] An instance is the only thing that runs; the mount fills `Source`.
- [x] A code pane drawn by hand, with the caret, selection and clipboard.
- [x] Tabs in the central dock node, siblings of the Viewport, draggable out.
- [x] Luau colour from Luau's own lexer, incremental.
- [x] Find, replace, go to line, and syntax errors underlined.
- [x] Autocomplete from `ClassRegistry`, with the IDL's prose.
- [x] A debugger: breakpoints, continue, step over/into/out, stack and locals.
- [x] Ctrl+S writes where the instance came from, and the editor reloads.
- [x] Eight syntax tokens in the theme, measured.

## NOT in scope

Type inference. Editing a variable from the debug panel. An external-debugger
protocol. Multi-cursor, folding and a minimap. Native codegen — which would
silently take breakpoints and locals away, and is now a written debugger
invariant rather than a surprise.

## Subagent plan

One Explore pass across three areas before a line was written, and one Plan
agent on the two riskiest pieces — the text widget and the debugger. Everything
after that was orchestrator work: `MASTER_PROMPT.md` §7 forbids fanning out
across two modules' seams, and this milestone is one seam per step.

## Findings

**Finding 1 — the model was half-built, and finishing it was the first step
rather than a detour.** ADR 0050 decided a script's source is a property of the
instance; the mount never wrote it and `startScripts` never read it. Files that
ran without a `Source`, and instances with a `Source` that never ran. **A tab
cannot edit "the script" while there are two of them**, so the milestone started
by making the instance the only thing that runs. The world hash moved, as
ADR 0050's own change had, and the traces were re-recorded on both tiers.

**Finding 2 — `InputTextMultiline` was ruled out by the vendored source, not by
taste.** It renders the buffer itself in one colour with no per-token hook; its
multiline path has **no length guard at all** (`imgui_widgets.cpp:5512-5515`
says a pathologically long line "would still crash"); and `ImGuiInputTextState`
is a single object per context, so N open documents cannot each keep a caret.
Reading that before writing anything is what turned "which widget" into a
five-minute decision.

**Finding 3 — the lexer can be run over one line, and that decided the whole
data structure.** `Lexer`'s constructor takes a `startPosition` it derives its
line counter from, and `peekch` is bounds-checked so no NUL terminator is
required. So a `string_view` into ONE line is a legal buffer reporting absolute
positions. That is why the document is a vector of lines rather than a flat
buffer: it is the only shape with somewhere to hang the per-line lexer state,
and the state is why an edit costs one line.

**Finding 4 — a hand-drawn text widget gets no characters until it asks for
them, and nothing in the API is named to suggest it.** SDL3 delivers no text
until `SDL_StartTextInput`; the only thing that calls it is ImGui's backend
reading `PlatformImeData.WantTextInput`; and the only thing that sets that is a
widget doing it for itself (`imgui_widgets.cpp:5700-5709`). Found by driving the
real window: every arrow key worked, the caret moved, and not one character ever
arrived.

**Finding 5 — the OS can drive this editor, which contradicts a limit five
milestones have recorded.** E1 found that "SDL does not accept injected input"
and every milestone since repeated it. That is true of ImGui-level injection and
**false of `SetCursorPos` + `mouse_event` + `SendKeys`**: real Win32 input
reaches SDL exactly as a person's does. Three of the defects below were found by
driving the window from a script and looking at the result. It is not a
replacement for a person — it cannot judge whether a colour is pleasant — but
"there is no automated path to a click inside the editor" should stop being
repeated.

**Finding 6 — three things about `LOP_BREAK` that the design had to learn.**
The deferred queue does not resume a parked thread: `enqueueTaskCallback` treats
it as a thread waiting to START, and the script simply never continued.
`LOP_BREAK` re-executes on resume — `pc` still points at the patched instruction
and `lua_resume` clears the status the hook set — so Continue parked on the same
line forever until exactly one skip was added. And the single-step hook fires
before anything has moved, so every step stopped where it started. All three
were found by tests, and none of them is guessable from the header.

**Finding 7 — `lua_stackdepth` answers 1 inside a function the chunk called.**
Not the number of frames a reader would count. Written into the test rather than
worked around, so the next person does not spend the same half hour.

**Finding 8 — a column is bytes and a monospace cell is a codepoint, and every
ASCII test passes either way.** Reported by the human as "it puts extra spaces
when I type á". `á` is two bytes and one glyph, so a pane placing runs at
`byteColumn * advance` left a gap the width of a space after every accented
letter, put the caret one cell right of the character it was on, and made a
click land on the wrong one. **This class of defect cannot be found by a test
written in English**, which is why the new cases assert the round trip over a
two-byte and a four-byte codepoint.

**Finding 9 — AltGr is Ctrl+Alt on Windows.** It is how a Brazilian, German or
Polish keyboard types half its punctuation, and read as a Ctrl chord it made
AltGr+something silently mean Select All, Paste or Save. One condition.

**Finding 10 — a save must put back everything a reload throws away, and there
were four things.** The panels (`overlayVisible` is world state and a fresh
`EngineState` says false — so saving a script left the editor as a bare viewport
with no menu bar); the open tabs; the Explorer selection; and which rows of the
tree were expanded. All four are restored by **chunk name**, which means the same
thing in the new world where an instance id does not. The `overlayVisible` half
was a defect the dev server had had since M3 and nobody had noticed, because
`luaug dev` starts with the overlay hidden anyway.

## Attempted / abandoned

**Resuming through the deferred queue.** Recommended by the design pass so the
script would restart in the scheduler's order, and it does not work: see Finding
6. The debugger resumes directly, at the safe point, which is where a drain
would have run anyway.

**Binding F3 to "find next".** The shell toggles its furniture on F3 by reading
platform events, which an active ImGui item does not suppress — so it would have
hidden the editor mid-search.

## Gate Record

Filled 2026-08-24, before human review.

| Gate item | Result |
|---|---|
| A script that is an instance runs | **Green.** Four cases in `world_host_tests.cpp`: a `Script` the scene brought runs, a disabled one does not, a `ModuleScript` still only runs when required, and a mounted script carries its file in its own `Source`. |
| An edit costs the lines it reached | **Green, as an equality.** One character in a 200-line file and in a 20,000-line one both re-lex **1**. `--[[` at the top of a 500-line file re-lexes 500, once; the next keystroke inside it re-lexes 1; closing it re-lexes 499. |
| A cell is a codepoint and a column is bytes | **Green.** Round-trip asserted over a two-byte and a four-byte codepoint, both directions, plus the ASCII case that made this invisible. |
| The debugger stops, reads and resumes, headless | **Green.** Eight cases in `debugger_tests.cpp`, including **the tick keeps advancing while a script is parked** — the claim a blocking debugger would fail while looking identical everywhere else. |
| Completion answers from the reflection tables | **Green.** Eleven cases, including inherited members, the IDL's prose, case-insensitive filtering, and the absence it deliberately has. |
| Eight syntax tokens clear 4.5:1 | **Green.** Sixteen assertions (8 tokens × 2 themes) against the code pane's ground, by the same `contrastRatio` the interface tokens use. |
| A person writes a script in it | **Partly, and honestly.** The pictures below are the editor open on a real project with a script in a tab, coloured, with a breakpoint armed and the Debug panel showing it. **What is not pictured is a stop**: the debugger's behaviour rests on the eight headless cases, and a photograph of it stopped with the locals showing needs somebody to press play. |
| `scripts/localgate.ps1` green on every stage | *(filled below)* |

### The pictures

`docs/images/e8/editor-dark.png` and `editor-light.png` — the same editor in both
themes: `init` as a tab beside `Viewport`, Luau coloured by Luau's own lexer,
line numbers and a breakpoint dot in the gutter, and the Debug panel docked
beside Content and Console showing `src/scripts/init.luau:11 (not bound)`.

**"Not bound" is the picture's most useful detail.** The world is not running, so
there is no `Proto` to patch — and the panel says which rather than drawing a
filled marker that would never fire. That distinction is free: Luau's
`lua_breakpoint` answers the line it really landed on, or -1.
