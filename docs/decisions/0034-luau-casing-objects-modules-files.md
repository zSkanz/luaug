# 0034 — Luau casing: objects vs modules, and the file-level rule

- Status: accepted
- Date: 2026-08-19

## Context
`api-design.md` §9 described the public casing convention as a split between
"the world" and "things you require". That framing is accurate about the
outcome and wrong about the reason, and it left two questions unanswered often
enough to be worth settling: whether LuauG's own `@luaug/*` libraries follow the
world's PascalCase, and how identifiers *inside* our Luau files should be cased
— a thing no document said at all, with roughly 800 lines of first-party Luau
already written to nobody's stated rule.

The human settled both directly, and confirmed the public half is the Roblox
convention.

## Decision
**The public rule, restated by what it actually keys on:** *if you index it off
an object, it is PascalCase; if you index it off a module or namespace, it is
camelCase.*

- Object members — properties, methods, events reached through an instance or a
  value — are PascalCase: `part.Name`, `part:Destroy()`, `cf:Inverse()`,
  `v.Magnitude`.
- Module and namespace functions are camelCase, **constructors and factories
  included**: `Instance.new`, `CFrame.fromEuler`, `Color3.fromRGB`,
  `Signal.new`, `task.spawn`, `vector.create`, and whatever `@std/*` and
  `@luaug/*` export. Namespace constants likewise: `CFrame.identity`,
  `vector.zero`.
- Type, class and namespace names are PascalCase.

This is the same surface the document always described; the reframing is what
makes it decidable at the call site instead of requiring a category judgement
about what counts as "the world". It also answers the `@luaug/*` question: those
are modules, so they are camelCase.

**The file-level rule**, which is new:

- Module-level variables and constants are **PascalCase with no underscores** —
  no `SCREAMING_SNAKE_CASE` anywhere in this codebase.
- Locals and inner functions inside a function body are camelCase.
- Module-level functions, helper and exported alike, stay camelCase, since a
  module is not an object.
- Types are PascalCase.

## Consequences
No public API name changes: `Instance.new` and `Color3.fromRGB` are what they
always were, which is also what someone arriving from Roblox will type without
thinking. The generator's naming lints (api-design §9) encode the object/module
distinction on the IDL, so the surface cannot drift one property at a time.

The file-level rule is retrofitted across the existing first-party Luau in the
same milestone that states it — a convention introduced with exceptions already
in the tree is a convention nobody follows.

An earlier revision of this ADR widened the public rule to PascalCase
everything, constructors and constants included. That went further than
intended and is recorded here rather than quietly rewritten, because the
reasoning against it is the useful part: `Instance.New` buys nothing, costs the
Roblox muscle memory the project is built to serve, and would have forced a
divergence row for a change no user asked for.
