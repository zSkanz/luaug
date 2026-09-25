# 0093 — The script editor types code with Luau's own checker

- Status: accepted
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
