# The debug overlay

Press **F3**. A panel appears over the running game with the engine's own
instrumentation in it.

```luau
--!strict
local DebugService = game:GetService("DebugService")

DebugService.OverlayVisible = true
DebugService:ShowPanel("Streaming")
```

The overlay has two authors — the F3 key and `DebugService.OverlayVisible` — and
it **starts off**, shipped builds included. A debug overlay that greets everyone
who starts the engine is in the way.

## The panels

Five, and the list is closed. Asking for a name outside it raises rather than
doing nothing:

| Panel | Shows |
|---|---|
| `Stats` | Frame time, draw calls, instance and body counts, and your own numbers. |
| `Scene` | The instance tree. |
| `Log` | What `print`, `warn` and the engine have said. |
| `Streaming` | Chunk states, foci, residency. |
| `Physics` | Bodies, contacts, the solver. |

`DebugService.ShowPanel` and `DebugService.HidePanel` take those names.

## Stats

```luau
--!strict
print(DebugService:GetStat("FPS"))
print(DebugService:GetStat("DrawCalls"), DebugService:GetStat("VisibleObjects"))
```

| Name | Is |
|---|---|
| `FPS` · `FrameTimeMs` | The frame. |
| `DrawCalls` · `VisibleObjects` · `InstancedDraws` · `MeshLodDraws` | The renderer. |
| `InstanceCount` · `PhysicsBodies` | The world. |
| `LuaMemoryKB` | The script heap. |
| `AudioVoices` · `AudioUnderruns` · `AudioClipsLoaded` · `AudioClipsMissing` | Audio. |

**An unregistered name raises** rather than returning zero — a stat that
silently reads zero is a measurement nobody can trust. A stat that is
*structurally* zero, though — no physics world, headless audio — returns zero as
the truthful answer.

`DrawCalls` against `VisibleObjects` is the whole story for instancing: a run of
objects sharing a mesh and a material is one call.

### Your own numbers

```luau
DebugService:SetCustomStat("EnemiesAlive", #enemies)
```

They appear on the `Stats` panel and read back through `GetStat`. Engine names
are checked **first**, so a game cannot shadow one.

## Drawing in the world

```luau
--!strict
local DebugService = game:GetService("DebugService")

DebugService:DrawLine(from, to, Color3.fromRGB(255, 80, 80))
DebugService:DrawBox(part.CFrame, part.Size)
DebugService:DrawSphere(hit.Position, 0.25)
```

All three last **one frame**, so a gizmo is drawn from a per-tick handler rather
than created and destroyed.

Headless, they are a **silent no-op** rather than an error — debug drawing left
in shared code cannot fail a headless test.

## The log

```luau
--!strict
DebugService.MessageOut:Connect(function(message: string, level: EnumLogLevel)
    if level.Value >= Enum.LogLevel.Warning.Value then
        recordForCrashReport(message)
    end
end)
```

`print` and `warn` each produce exactly one fire, with their text verbatim, at
`Info` and `Warning`. **Engine messages arrive pre-formatted with their key in
front** — so a handler matching on a key sees engine output, and one matching
prose sees script output.

Contained handler errors come through here too, which is how you find an error
in a signal handler that did not stop anything.

`Enum.LogLevel` ascends: `Trace`, `Debug`, `Info`, `Warning`, `Error`. Its
`Value` orders them, which is what the comparison above uses.

## In a shipped build

`DebugService` is **present** in a shipped build — it is not a development-only
service. What is compiled out is the ImGui overlay itself, so the methods become
no-ops rather than absent, and the frame loop needs no conditional.

That is deliberate: a game can offer the overlay to its own players if it wants
to, and debug-draw calls left in shared code cost nothing.

The overlay is also inert wherever there is nothing to draw with — a headless
run, a capture run, the null renderer. That is a normal outcome rather than a
failure.

## Where to look next

- [Graphics quality settings](manual:rendering/quality) — the settings those
  numbers move
- [`DebugService`](api:DebugService)
