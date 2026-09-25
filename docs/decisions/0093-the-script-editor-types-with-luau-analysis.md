# 0093 — The script editor types code with Luau's own checker

- Status: accepted; amended 2026-09-25 (the new solver -- see the end)
- Date: 2026-09-24
- Amends: [0057](0057-a-script-is-an-instance-and-the-editor-edits-one-thing.md)
  §5 (autocomplete comes from the engine's own reflection, not from Analysis)
- Relates to: [0002](0002-luau-pinned-direct-embed.md) (no Analysis in the
  runtime binary), [0018](0018-strict-luau-new-solver.md) (strict, new solver,
  checked by `luau-analyze`), [0092](0092-a-script-lives-in-the-instance-it-is-put-in.md)

## Context

ADR 0057 §5 kept `Luau.Analysis` out of the build. Its price was 35% of a cold
compile, and the script editor was expected to manage without inference: the
class registry, the world's tree and a reader of the file's own AST for a few
shapes (`sourceMembersOf`). It said so plainly: "It does not infer types across
expressions, and that limit is stated here so nobody reads the absence as a
bug."

People who use the editor read it as a bug anyway, because every editor they
compare it with does infer. The owner's reports on one day:

- a `Snake` type defined in a `ModuleScript` in `ReplicatedStorage` is unknown
  in the script that requires it;
- `Signal` is not typed;
- the parameters of the function being called are not shown while typing its
  arguments;
- "lembre-se, vamos fazer tudo de maneira profissional."

Each one is a separate extension of the pattern reader: `require` across
files, generics, datatypes, a function's type at a call. Together they add up
to a type checker. The pattern reader cannot get there, and a second type
checker written here would be worse than the one Luau already ships at the
version this engine pins.

## Decision

**The script editor's language service is `Luau.Analysis`**, at the pinned
Luau, reading the same definitions `luau-lsp` and `luaug check` read
(`runtime/types/engine.d.luau`, generated from `api/defs`).

- **Editor builds only.** It is linked under `LUAUG_DEBUG_UI`, the switch that
  already keeps ImGui and `assetc` out of a shipped game, and never by the
  shipping or player profile. ADR 0002's rule, "no Compiler or Analysis in the
  runtime binary", stands for every binary that ships a game.
- **A module is an instance.** The frontend's `FileResolver` resolves
  `require(script.Parent.X)`, `require(game.ReplicatedStorage.Snake)` and
  `WaitForChild` / `FindFirstChild` steps against the open world, and reads
  `Source` from the instance. A script mounted from `src/scripts` is the same
  instance and is read the same way. Every script is one module name,
  `treePathOf`'s dotted path, so a require of an unsaved buffer sees what is
  typed rather than what is on disk.
- **What it answers**: completion (members, globals, types, with inferred
  types), signature help (the called function's parameters, the active one
  highlighted, and its doc from the definitions), hover, and type errors under
  the code beside the parse errors. The world-tree completion from 0057 stays
  as it was: a child of an instance is a fact about the tree, which a type
  checker cannot know. The analyzer's answer and the tree's answer are merged.
- **The old solver, at this pin, and measured rather than preferred.** The new
  solver stops at the definitions' `Instance.new`, one intersection of every
  creatable class's overload, with "code too complex". That is neither a
  budget nor a flag: raising every Analysis limit a hundredfold and turning
  every flag on still fails, and the same file without that intersection
  loads. The old solver loads the whole file and answers every case below.
  The editor moves to the new solver with the pin that can, and a test pins
  the load, so that pin cannot go unnoticed. ADR 0018's rule is about the
  code a project ships, which `luaug check` still checks under the new
  solver. This decision is about the assistance the editor gives while it is
  being written.
- **A module's text comes from its buffer.** A tab writes `Source` through
  the inspector at the next frame's safe point, so the world is one keystroke
  behind. The snapshot a request carries takes the asking tab's text from its
  buffer.
- **An error type is shown as it was written.** A parameter annotated with a
  type the module never declares (`gridPos: Position`) is an error type to
  any checker. The signature shows `Position`, and the check underlines it as
  unknown, rather than printing `*error-type*`. Completion off a value whose
  declared type is an error offers what the function that made it returned.
- **Checked off the frame.** A check runs on a worker when the text stops
  changing, and the pane keeps the previous answer until a new one arrives. A
  frame never waits on the checker.

The pattern reader (`sourceMembersOf`) remains as the fallback while the first
check is still running. Once the analyzer is in, it is removed.

## Consequences

- A cold build of an editor-capable profile gets slower by about the 35% that
  0057 measured: 34.8 s to 53.9 s on twenty cores. Incremental builds do not
  change, because Analysis is a vendored library that is rebuilt only when the
  pin moves. The shipping and player profiles do not change at all.
- The editor's typing is the same as what `luaug check` and `luau-lsp` report,
  because the checker and the definitions are the same. Before this, three
  tools could disagree.
- Every change to the definitions that the generators emit is now a change
  the editor sees, which ties the generated `.d.luau` to editor behaviour. Its
  tests check that the file loads without errors.

## Amendment, 2026-09-25: the new solver

The owner: "we should use the New Solver, not the old one". The editor's
checker now runs `SolverMode::New`, which is R2's rule and the reference
editor's own, and three things that decision above measured are answered:

- **`Instance.new` is a magic function, not forty-odd overloads.** The new
  solver refuses the intersection as "code too complex" -- so does `luau-lsp`'s
  newer Luau, measured the same day, so it is the shape and not the pin. The
  reference platform's definitions declare `Instance.new(className: string) ->
  Instance` and type the literal call in C++; this does the same: the
  definitions keep their overloads (every other reader uses them), the editor
  loads them with one signature in their place, and `MagicInstanceNew` answers
  a literal class name with its class. A generic over a map of names
  (`index<Map, K>`) was tried first and widens the literal to `string`.
- **One module and one set of globals.** The new solver checks and completes
  from the same module, so the second copy the old one needed is gone.
- **What a signature shows is what was written**: a parameter or a return
  that is annotated is shown as its annotation, and a function declared
  without `...` does not show the `...: any` tail the new solver gives it.

Two behaviours move with it, both towards the reference: a module cast to the
type it promises (`return M :: { ... }`) is no longer an error when a member
does not match -- the new solver allows the cast in nonstrict and strict alike,
as the reference editor does -- and Luau's own linter now runs beside the
checker, with the reference's set (the defaults, less the three unused-name
lints it disables and the unknown global the checker already reports), which
is what reports a field written twice in a table or a table type.

`luaug check` still runs `luau-lsp` without the new solver's flag, which D192
records: under that flag the definitions' `Instance.new` is "code too complex"
there too, and `luau-lsp`'s standard platform has no magic function to put in
its place.
