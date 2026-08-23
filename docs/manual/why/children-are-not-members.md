# Children are not members

```luau
workspace.Baseplate          -- raises scene.err.unknown_member
workspace:FindFirstChild("Baseplate")   -- this
```

Dot access reaches **members**: properties, methods and events the class
declares. It does not fall back to searching children.

## Why not

Because an index that could be either is untypeable.

The engine's whole API is declared once and generated into type definitions the
analyzer reads, and every project is written in strict mode. That is what makes
`part.Anchorred = true` an error at analysis time rather than a surprise at
runtime.

If `workspace.Baseplate` could resolve to a child, the analyzer would have to
answer "what type is `.Baseplate`?" — and it cannot, because the answer depends
on what somebody parented at runtime. The only sound answer is `any`.

So the typing story would leak out through **the most-used syntax in the
language**. Every tree walk would be untyped, and the strictness would be a
promise the API kept everywhere except where it was used.

## The second reason

It is also the habit that made an awaiting call load-bearing in the first place.

A name that resolves or errors depending on load order is not a lookup, it is a
race. `workspace.Baseplate` works when the baseplate happens to exist yet, and
raises when it does not — and the fix people reach for is to wait for it, every
time, everywhere, until waiting is threaded through code that has nothing to do
with loading.

Making the lookup explicit makes the question explicit:

```luau
--!strict
local found = workspace:FindFirstChild("Baseplate")   -- may be nil, and you say so
local ready = workspace:WaitForChild("Baseplate")     -- I will wait, deliberately
```

The first returns `Instance?` and the type system makes you handle the `nil`.
The second yields, and it is obvious in the code that it does.

## What the error tells you

`scene.err.unknown_member` names the member you missed, which is the point: it
distinguishes "you meant a child" from "you spelled a property wrong". Two
different mistakes with two different fixes, and dot-access-to-children would
have made them one message.

## What to use instead

| Want | Use |
|---|---|
| One child by name, maybe | `Instance.FindFirstChild` |
| One child by name, eventually | `Instance.WaitForChild` |
| The first of a class | `Instance.FindFirstChildOfClass` or `Instance.FindFirstChildWhichIsA` |
| All of them | `Instance.GetChildren` · `Instance.GetDescendants` |
| A *set*, wherever it is | `TagService.GetTagged` and `TagService.GetInstanceAddedSignal` |

That last row is the one worth internalising. If you are reaching for a child by
name to identify a *category* of thing, you wanted a tag — and a tag also works
for instances that do not exist yet.

## A name is not a key anyway

Siblings may share a name. `FindFirstChild` returns the first in document order
and nothing enforces uniqueness, so a name is a label rather than an identifier
— which is another reason building lookups on it is building on sand.

## Where to look next

- [The Instance tree](manual:concepts/instance-tree)
- [Properties, attributes and tags](manual:concepts/properties)
- [The migration guide](manual:roblox/migration)
