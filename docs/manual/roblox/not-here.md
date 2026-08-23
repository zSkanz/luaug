# What is not here

Written so you find out now rather than three weeks in.

Everything below is genuinely absent. Where something is *planned*, it says so;
where it is not, it says that too.

## The platform

| Missing | State |
|---|---|
| Accounts, a players service, friends, avatars | **Not planned.** This is an engine, not a hosted platform. |
| Data stores | **Not planned.** Persistence is a backend you write. |
| Marketplace, monetization, analytics | **Not planned.** |
| Matchmaking, servers, replication | **Planned**, as a later phase. There is none today. |

The shape of the replacement is one HTTP client and your own server. See
[Talking to a backend](manual:guides/backend).

## Rendering and world content

| Missing | State |
|---|---|
| Particles | **Planned**, next phase. The most visible gap here: no fire, smoke, sparks, dust or impact, and no way to fake it. |
| Decals | **Planned**, next phase — and as a projected thing in the world rather than a face of a part, so a bullet hole is expressible. |
| Terrain | **Planned**, next phase. The open question is authoring. |
| World-space UI and billboards | **Planned**, next phase. The UI tree exists; putting its output somewhere other than the screen does not. |
| Rich text | **Not scheduled.** A label renders its codepoints left to right; complex scripts are not shaped. |
| Skyboxes and custom environments | **Not present.** The sky is analytic, from `Lighting`, and it is also the reflection environment — right outdoors and wrong in a cave. |
| Screen-space reflections | **Not scheduled.** What ships is image-based lighting from that sky. |
| Shadows from point and spot lights | **Stored and not yet acted on.** The sun is the only caster. |
| `BasePart.Material` | **Not shipped.** A surface look rather than body state; a property nothing reads would look like a working API. |
| Temporal anti-aliasing, upscalers, frame generation | **Not present**, and blocked on a velocity buffer that does not exist. |
| Motion blur, depth of field, colour grading | **Not present.** |

## Physics

| Missing | State |
|---|---|
| Every constraint except a rigid weld | **Not scheduled.** No hinge, no spring, no motor, no solver joint. |
| `Attachment` | **Not present.** A weld carries its offsets directly. |
| Concave mesh colliders | **Accepted and not implemented** — `Precise` reads back and behaves as a convex hull. |
| A sleep or wake API | **Not present.** Sleeping is real internally and is not scriptable. |
| Per-part gravity, velocity clamps | **Not present.** `Workspace.Gravity` is the knob. |

## Interface

| Missing | State |
|---|---|
| `UIGridLayout`, `UIScale`, `UIStroke`, `UIGradient`, `UIAspectRatioConstraint` | **Not scheduled.** Three modifiers is the set: list layout, padding, corner. |
| Borders | **Absent**, deliberately. |
| Selection, clipboard, undo in a text field | **Not present.** Typed text, backspace and a caret. |
| A reactive UI framework | **Not the engine's.** The engine ships the instance tree. |

## Scripting

| Missing | State |
|---|---|
| A filesystem for scripts | **Not present.** No reading, no writing, no save file. |
| Most of the standard library in the game VM | **Not present yet.** Only an HTTP client is registered; JSON, paths and string helpers are not reachable from a script. |
| A localization service | **Not present.** The catalog format exists and nothing loads a game's catalog. |
| `loadstring` and friends | **Never.** Sandbox. |

## Tooling

| Missing | State |
|---|---|
| Targets other than 64-bit Windows | **Not present.** `luaug build` refuses the rest rather than approximating one. |
| Mobile | **Planned**, a later phase. |
| Asset hot-swapping | **Reserved.** The protocol has a message for it and the engine answers "not implemented" rather than ignoring it. |
| Viewport manipulators in the editor | **In progress.** Transforms are edited by typing numbers today. |
| A package manager | **Not present.** |

## Two things that are thinner than they look

Worth calling out separately, because both *appear* to work:

**A sound's timeline is one second long, whatever the file is.** Audio decodes
and plays, but the timeline the engine counts against is fixed — so a looped
sound repeats after one second, and `Sound.Ended` fires after one second
whatever the clip.

**`AudioService.PlayLocal` does not clean up after itself.** The sound it makes
stays parented to the service once it has ended.

## What to take from this

The engine is honest about its edges, and the list above is the evidence. A gap
here is a gap with a state next to it rather than a surprise waiting in week
three.

If something on this list is load-bearing for what you want to build, it is
better to know before the first line than after the hundredth.

## Where to look next

- [What LuauG is](manual:get-started/what-is-luaug)
- [Every deliberate divergence](manual:roblox/divergences)
