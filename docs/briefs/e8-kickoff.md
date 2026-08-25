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
- [x] Autocomplete from `ClassRegistry`, with the IDL's prose -- and from the
      WORLD: a path walks the real tree, and `WaitForChild("` names real children.
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

**Finding 10 — six patches in a row were one wrong decision, and the patches
were the tell.** Ctrl+S rebuilt the world so a running VM would pick the change
up. Then, one report at a time: the panels vanished, the tabs closed, the caret
jumped to line one, the Explorer collapsed, the selection was lost, and the
screen flashed. Each was fixed — by putting the panels back, re-opening the tabs
by chunk name, restoring the caret, keeping the expansion, re-selecting.

**Every one of those fixes was compensation for something that should not have
happened.** Saving a text file must not destroy a world, and it never needed to:
the pane writes `Source` as somebody types, so the world already holds the text,
and ADR 0058 makes play compile `Source` at the moment it starts a script. In
the editor there is no running chunk to refresh.

Ctrl+S is now a `writeTextFile` and nothing else, and five of the six patches
were deleted with the reload that made them necessary. The sixth was real and
kept: `overlayVisible` is world state, so a fresh `EngineState` closes the
panels — a defect the dev server has had since M3, which nobody had noticed
because `luaug dev` starts with the overlay hidden anyway.

**The lesson is the shape rather than the bug.** A fix that has to put six
unrelated things back is not a fix, it is a receipt for the wrong operation.

**Finding 11 -- completion from the API alone answers the question nobody was
asking.** The list knew every class the engine ships and not one thing in the
project open in front of it, so `Workspace.` offered `ClassName` and never
`MainCamera`. The names somebody reaches for are the names in their own
Explorer, and the Explorer is a pointer away. A path is now walked instance by
instance from the DataModel -- which also means there is **no list of service
names in the completion code at all**: a service is a child of the root, so one
registered by a module this build has never seen resolves for free.

**Finding 12 -- a popup measured in one font and drawn in another leaks, and
every ASCII string hides it by a different amount.** `ImGui::CalcTextSize`
measures in the CURRENT font, which is Inter; the code pane draws with
`m.font`, which is Cousine. Reported as "the text spills out of the blue". The
pane multiplies cells by one advance everywhere else for exactly this reason,
and the popup was the one place that had asked ImGui instead.

**Finding 13 -- a widget that holds ImGui's active id makes every other item in
the frame unhoverable, and the cost is one CLICK rather than one frame.**
`ItemHoverable` returns false while another item is active (`imgui.cpp`), so the
first click on the Explorer, the Viewport, the Properties grid or another tab
did nothing and only the second one landed. Releasing the active id at the top
of the frame is necessary -- whichever panel was submitted earlier has already
been asked whether it was clicked, and answered no -- and it is **not
sufficient**. Six lines further down the same function: an item carrying
`ImGuiItemFlags_AllowOverlap`, which every Explorer row does, is hoverable only
if it was ALREADY the hovered id on the PREVIOUS frame. While the caret lived in
the pane no row could hover, so on the frame the block was lifted no row had the
history it needed either. The answer is to declare what is true --
`ActiveIdAllowOverlap` -- so hover is resolved underneath the caret and the row
is already hovered when the press arrives.

**And the first attempt was signed off on a misread picture**, which is the part
worth keeping. The screenshot showed the clicked row highlighted; it was the
HOVER highlight, and the old selection was still sitting two rows down in the
same shot. D101 had recorded the same lesson the day before -- a fix verified by
reading the call site rather than by watching the pixel -- and this is the
version of it where the pixel was watched and read wrong. What settled it was
instrumenting the running window and printing what ImGui itself thought on the
click frame.

**Finding 14 -- a property row is one line high and a script is not.** The grid
drew every line of `Source`, so selecting a script made the Properties panel as
tall as the file. Fixed by giving the IDL a `Code` flag rather than by testing
whether the string has a newline in it: which properties are code is a fact
about the CLASS, and `PropertyDesc::contentKind` had already made that argument
in its own comment for the same reason.

**Finding 15 -- `IsMouseDragging` with a zero threshold is true for as long as
the button is down.** So a double-click selected the word and the very next
frame put the caret back under the pointer, leaving the half of the word left of
the click -- reported as "double-clicking in the middle of `require` selects up
to the `u`". A drag now knows what it is extending BY, and a word-wise drag
grows a word at a time in either direction.

**Finding 17 -- writing a list from the library's own source is not the same as
reading it from the VM, and the test found both ways it can differ.** The
standard-library surface was transcribed from
`third_party/luau/VM/src/l*lib.cpp` rather than remembered, which felt
authoritative and was wrong twice. `math`'s constants -- `e`, `nan`, `phi`,
`sqrt2`, `tau` -- are set with `lua_setfield` rather than listed in the
registration table, so reading the table missed five names. And
`buffer.readinteger` / `writeinteger` appear in that file inside a block this pin
does not build, so a careful read of the source would have offered two functions
no script can call. **A list checked against the artefact rather than against its
source is the only kind worth writing**, and the check runs in both directions
so a Luau bump fails rather than quietly ages the editor.

**Finding 16 -- `rebuilt.empty()` cannot tell "nothing yet" from "a blank
line".** Moving a line into the empty last line of a file swallowed the final
newline, because the join skipped the separator it had already decided not to
write. Counted rather than asked of the string. Every file ends in a blank line,
so this is the common case rather than the corner one.

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
| A line moves and one undo takes it back | **Green.** Four cases in `script_document_tests.cpp`: a swap in both directions, a block that keeps its order, the top and bottom answering false rather than doing nothing, and the blank last line of a file surviving a move into it. |
| The standard library is offered, and it is OURS | **Green, and checked against a real VM.** `sandbox_tests.cpp` boots a sandboxed VM and checks `stdlib.h` both ways: 205 names exist with the type they claim, and every key of all ten library tables is named. Plus four cases in `script_complete_tests.cpp`, including `os` having exactly three members and `getfenv`, `loadstring` and `spawn` being absent because the sandbox removes them. |
| Completion answers from the TREE | **Green.** Twelve more cases: a path through the world, the whole chain walked from `game`, `script.Parent`, a name inside `WaitForChild("`, `GetService("` offering only services, a colon offering no children, one row per name over four instances, and a plain string offering nothing. |
| A save writes a file and disturbs nothing | **Green, by a person and by a picture.** The editor after Ctrl+S is pixel-identical to the editor before it: same caret, same selection, same tabs, same tree. Verified by driving the real window twice and comparing. |
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
