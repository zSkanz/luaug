# `examples/01-instances` — the M2 deliverable

500 scripted cubes in a real Instance tree: five `Model`s of a hundred `Part`s,
reparented between two folders while they run, animated from
`RunService.Heartbeat`, and annotated with debug draw.

Run it:

```
luaug-host examples/01-instances                     # windowed
luaug-host examples/01-instances --headless --frames=120 --exit --screenshot=out.png
```

This is a **project directory**, not a single file, and that is part of what it
demonstrates. A directory is mounted per `docs/api-design.md` §4: every
`src/scripts/**/*.luau` becomes an entry `Script`, everything else is a module
reached through `require`, and `.luaurc` aliases resolve — `@shared/ring` here.
`examples/00-clear` is the other supported shape, a single file mounted as one
`Script`, which is what the render gates run.

## What it is showing

| In the picture | The thing being exercised |
|---|---|
| The ring of cubes | 500 instances under a `Folder` → `Model` tree, positioned every tick |
| A gap opening in the ring | a `Model` reparented to another `Folder` mid-run |
| The ring brightening as the gap opens | `AncestryChanged`, deferred — the brightness is written by the handler, never by the code that moved the model |
| The yellow box | drawn from `GetChildren` on the parked model, so the tree and the bookkeeping must agree |
| Its rhythm | a `task.delay` chain on the SimClock, not a tick counter |
| The grid | debug-draw gizmos: annotations no `GetChildren` can find |
