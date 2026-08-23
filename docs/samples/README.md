# docs/samples — the code the documentation shows

Each file here is a **real Luau file named after the symbol it demonstrates**:

```
Part.luau                 the class
Workspace.Raycast.luau    the member
Enum.PartShape.luau       an enumeration
task.luau                 a library
```

`api/generator/gen_site.luau` picks them up by name and renders them on that
symbol's reference page.

## Why a file rather than a field in the API definition

Because a sample has to compile.

The definition files carry one doc string per member and nothing else, so a
sample authored there would be a string — unchecked, unformatted, and free to
drift from the engine it describes. Here it is an ordinary `.luau` file, so:

- `luaug check` type-checks it against `runtime/types/engine.d.luau`, the same
  definitions a project is analysed against;
- the formatter formats it;
- and the site generator **fails the run** if a file names a symbol the API
  definition does not declare.

A sample cannot go stale against a renamed member without something going red.

## Writing one

Start with `--!strict`; the generator strips that line before rendering, because
the manual says once that every file carries it.

Keep it to the shortest thing that is genuinely useful — a reference page's
sample answers "how do I start", not "how do I finish". The manual is where a
worked example belongs.
