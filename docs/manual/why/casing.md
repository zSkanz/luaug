# Objects are PascalCase, namespaces are camelCase

One rule, and it decides every name in the API:

> **Index it off an *object* and it is PascalCase. Index it off a *module or
> namespace* and it is camelCase.**

```luau
part.Name                    -- off an object
part:Destroy()               -- off an object
v.Magnitude                  -- off a value
track.Looped = true          -- off a handle

Instance.new("Part")         -- off a namespace
CFrame.fromEuler(0, 1, 0)    -- off a namespace
Color3.fromRGB(200, 90, 40)  -- off a namespace
CFrame.identity              -- off a namespace
task.spawn(fn)               -- off a namespace
```

Type and class names are PascalCase. Constructors are camelCase, because a
constructor is reached off the namespace rather than off an instance of the
thing it makes.

## Why a rule at all

Because the alternative is not "no rule" — it is a hundred small decisions, each
made by whoever was writing that member, and a surface where you have to
remember rather than derive.

With the rule, you can predict a name you have never seen. `Color3.fromHex` is
camelCase before you look it up, because it is reached off `Color3`. `Sound.Play`
is PascalCase before you look it up, because it is reached off a sound.

## Why this rule

Because the distinction it encodes is a real one, and it is one you want to see
at the call site.

`CFrame.new(v)` and `cf:Inverse()` are different kinds of operation: the first
is a factory on a namespace, the second is a question asked of a value. Spelling
them the same way hides that. Spelling them differently means the shape of a
line tells you which half of the API you are in.

It also settles the two cases that otherwise become arguments:

- **Constants are camelCase.** `CFrame.identity`, `vector.zero`. They are
  namespace members, not instance members.
- **Library functions are camelCase.** `task.wait`, and everything the `@std`
  and `@luaug` namespaces export — because a module is a namespace and not an
  object.

## Inside your own code

The same rule, extended one step:

- **Module-level variables and constants are PascalCase, with no underscores.**
  No screaming snake case anywhere.
- **Locals and inner functions are camelCase.**
- **Module-level functions stay camelCase**, because a module is not an object.

```luau
--!strict
local Greeting = {}

local DefaultName = "world"          -- module-level constant

function Greeting.forPlayer(name: string?): string   -- off a module: camelCase
    local who = name or DefaultName                  -- local: camelCase
    return `Hello, {who}`
end

return Greeting
```

## The one place it does not apply

Metamethods. `__add`, `__mul`, `__eq` — the names are the language's and are not
the engine's to choose.

## Booleans

A boolean **property** carries no `Is` prefix; a boolean **method** does.

```luau
sound.Playing                -- a property: a fact
instance:IsA("BasePart")     -- a method: a question
```

That is why `AnimationTrack.Playing` is spelled the way it is rather than
`IsPlaying`, and why the hot-reload service's `IsReload` is a method rather than
a property. The rule was applied to itself, and the first spelling lost.

## It is enforced

The naming rules are lints inside the API generator, and they run as part of the
gate. A member that breaks the rule is a red build rather than a review comment,
which is the only way a convention this size survives.

## Where to look next

- [Datatypes and units](manual:concepts/datatypes) — the rule in its densest form
- [Scripts, modules and requires](manual:concepts/scripts)
