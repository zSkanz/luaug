# Vector3 is the Luau vector

`Vector3` is not a table, not a userdata and not a class. It **is** Luau's own
native `vector` primitive: three wide, 32-bit float, with arithmetic implemented
in the virtual machine.

```luau
local a = vector.create(1, 2, 3)
local b = Vector3.new(1, 2, 3)
print(typeof(a))     -- "vector"
print(a + b)         -- VM arithmetic, no metamethod dispatch
```

## Why it matters

A vector is the single most-allocated value in a game. Position, velocity,
direction, offset, colour-as-a-vector, the argument to almost every spatial
call — several per object per tick.

Implemented as a table or a userdata, each one is a heap allocation, a
metamethod dispatch per operation, and eventual garbage-collector work. As the
VM's own primitive it is a register-resident value: **no allocation, no
dispatch, no collection**.

The engine is also built to be fast **without** native code generation, because
some platforms forbid it. On an interpreter, the difference between a primitive
and a metatable is not a micro-optimisation.

## What it costs

Three things, and they are all visible in the API:

**Components are lowercase.** `v.x`, `v.y`, `v.z` — because that is what the
primitive exposes, and the engine does not get to rename it.

`v.Magnitude` and `v.Unit` are PascalCase, because those are the engine's own
additions reached off a value. So one type carries both conventions, and the
line between them is exactly "did we add it".

**It is 32-bit, everywhere, and that is not negotiable per world.** Which is why
the transform a script touches — `BasePart.CFrame` — carries an **f64**
translation, and why a large world does its arithmetic there rather than through
`Position`. See [Floating origin](manual:assets/floating-origin).

**There is no `Vector3` class to extend.** A helper is a function in a module,
not a method you can add.

## The namespace is wide

Because a primitive cannot carry methods for everything, the namespace carries
them, and there are a lot:

```luau
vector.create · Vector3.new
Vector3.dot · Vector3.cross · Vector3.magnitude · Vector3.normalize
Vector3.lerp · Vector3.clamp · Vector3.min · Vector3.max
Vector3.floor · Vector3.ceil · Vector3.abs · Vector3.sign · Vector3.angle
Vector3.zero · Vector3.one
```

Several also exist as members reached off a value — `v:Dot(w)` and
`Vector3.dot(v, w)` are the same operation, and which you write is a matter of
which reads better where you are.

That split is the casing rule at work: off a namespace it is camelCase, off a
value it is PascalCase. See
[Objects are PascalCase, namespaces are camelCase](manual:why/casing).

## Vector2 is a different thing

`Vector2` is an engine value type, not a primitive — the language has no
two-wide vector. So it has full operator support through the ordinary
mechanism, and its components are **uppercase**: `Vector2.X`, `Vector2.Y`.

That inconsistency between `v.x` and `v2.X` is real, and it is the honest
signal: one of them is the language's and one of them is ours.

## Where to look next

- [Datatypes and units](manual:concepts/datatypes)
- [Transforms, pivots and units](manual:world/transforms)
- [`Vector3`](api:Vector3)
