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
| Matchmaking and hosted servers | **Not planned.** A match is hosted by a player's machine (`--host`) or a server you run (`--serve`); finding one is your backend's job. |

Replication and a game's own messages are here: see
[Multiplayer](manual:guides/multiplayer). Anything that outlives a match is one
HTTP client and your own server: see [Talking to a backend](manual:guides/backend).

## Rendering and world content

| Missing | State |
|---|---|
| Complex-script shaping | **Not scheduled.** A label lays its codepoints out left to right, so Arabic, Devanagari and Thai do not join. Rich text is here: `TextLabel.RichText`. |
| Skyboxes and custom environments | **Not present.** The sky is analytic, from `Lighting`, and it is also the reflection environment — right outdoors and wrong in a cave. |
| Screen-space reflections | **Not scheduled.** What ships is image-based lighting from that sky. |
| Temporal anti-aliasing, upscalers, frame generation | **Not present**, and blocked on a velocity buffer that does not exist. |
| Motion blur, depth of field, colour grading | **Not present.** |

## Physics

| Missing | State |
|---|---|
| Springs, ropes, prismatic joints | **Not present.** The joints are `BallSocketConstraint`, `HingeConstraint` and `FixedConstraint`, between two `Attachment`s, plus welds and ragdolls. |
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
| Hot-swapping a compiled asset | **Not present.** Under `luaug dev`, a loose file you save is reloaded on the next frame; a packed or compiled one reloads the bytes it was built into. |
| A package manager | **Not present.** |

## What to take from this

The engine is honest about its edges, and the list above is the evidence. A gap
here is a gap with a state next to it rather than a surprise waiting in week
three.

If something on this list is load-bearing for what you want to build, it is
better to know before the first line than after the hundredth.

## Where to look next

- [What LuauG is](manual:get-started/what-is-luaug)
- [Every deliberate divergence](manual:migrating/divergences)
