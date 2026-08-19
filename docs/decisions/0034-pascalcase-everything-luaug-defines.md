# 0034 — PascalCase for everything LuauG defines in Luau

- Status: accepted
- Date: 2026-08-19

## Context
`api-design.md` §9 originally described a **two-register** convention inherited
from Roblox and Lute: the world API (Instances, services, datatypes) in
PascalCase, and anything you `require` — including LuauG's own `@luaug/*`
libraries — in lowercase modules with camelCase functions. Constructors were
`Type.new(...)`, factories `Type.fromX(...)`, and constants lowercase
(`CFrame.identity`).

That split has a real cost: the same engine speaks in two voices, and the line
between them is invisible at the call site. `Instance.new` next to
`CFrame.FromEuler` is not a rule anyone can hold in their head, and Roblox's
lowercase `new` is a Lua-era inheritance rather than a design.

The human made the call directly, twice, the second time widening it
explicitly: *everything Luau-facing is PascalCase.*

## Decision
**Everything LuauG defines in Luau is PascalCase** — classes, services,
properties, methods, events, constructors, static factories, constants, and the
`@luaug/*` libraries alike. `Instance.New`, `CFrame.FromEuler`,
`CFrame.Identity`, `Vector3.Zero`, `require("@luaug/testing").Describe`. No
camelCase aliases, ever (that part of §9 is unchanged).

**Two surfaces are excluded because they are not ours to rename**, and renaming
them would break the thing they exist for:

1. **The Luau language's own libraries** — `task`, `vector`, `buffer`, `string`,
   `table`, `math`, `coroutine`, `utf8`, `os`. These are the language.
   Consequently the native vector's fields stay `x`/`y`/`z` (divergence #9):
   they belong to a VM primitive.
2. **`@std/*`** — its entire purpose is that utility code runs unchanged on
   Lute and Roblox (ADR 0030). `task` sits in both camps and stays lowercase
   because `@std/task` *is* the global `task` (api-design §7).

LuauG's own `Vector3` global is fully PascalCase (`Vector3.New`,
`Vector3.Magnitude`, `Vector3.Zero`); the lowercase `vector` stdlib remains
available beside it, unchanged, for code that must also run under Lute.

**Identifier casing inside Luau files** follows the same spirit: PascalCase for
anything declared outside a function scope (module locals, module-level
functions, exported members, types), camelCase for locals and inner functions
inside one, and constants PascalCase with no underscores — no
`SCREAMING_SNAKE_CASE`.

## Consequences
`Vector3.New` constant-folds exactly as `Vector3.new` did: `vectorCtor` is a
string the Luau compiler matches (ADR 0013), so the fastcall path is unaffected.

Divergence from Roblox grows by one row (#26) — which also widens the
clean-room distance ADR 0020 wants. The migration guide gains a casing note,
and the API generator's naming lints (api-design §9) enforce this on the IDL,
so the surface cannot drift back one property at a time.

The two exclusions are the part most likely to be revisited. They are recorded
as *why*, not as taste: if ADR 0030's convergence bet is ever dropped, the
`@std` carve-out goes with it.
