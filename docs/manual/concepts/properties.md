# Properties, attributes and tags

An instance carries data in three ways, and choosing between them is one of the
few genuinely architectural decisions a script makes.

| | Declared by | Typed | Found by |
|---|---|---|---|
| **Property** | The engine | Yes, statically | Knowing the class |
| **Attribute** | Your game | Yes, at runtime, from a fixed value domain | Knowing the name |
| **Tag** | Your game | It is a name, not a value | `TagService` |

## Properties

A property is part of a class's declared surface. The analyzer knows its type,
the engine acts on it, and naming one the class does not have raises
`scene.err.unknown_member`.

```luau
local part = Instance.new("Part")
part.Anchored = true
part.Size = vector.create(4, 1, 4)
part.Color = Color3.fromRGB(200, 90, 40)
```

**A write that changes nothing enqueues nothing.** Assigning a property the
value it already holds raises no signal at all — `Instance.GetPropertyChangedSignal`
reports a *change*, in the past tense.

**Some properties refuse a value rather than clamping it.** A property with a
numeric domain says so on its reference page — "Accepts a number from 0 to 1" —
and a write outside the domain raises with a key naming the whole domain rather
than merely the type. That is why `PhysicsService.FixedTimestep = 1/10` reports
a range and not "it takes a number".

**Read-only means read-only.** `BasePart.LinearVelocity` and
`BasePart.AngularVelocity` are outputs of the simulation; assigning one raises.
To move a body, either move it directly or push it — see
[Rigid bodies](manual:physics/bodies).

**A few properties are stored and not yet acted on.** The reference marks those
**stored, not yet acted on**, in orange, on the member itself. The marker exists
because a property that accepts a write and silently changes nothing is the
hardest kind of gap to notice: every way of checking it from a script agrees
with what you wrote.

### Watching one change

```luau
part:GetPropertyChangedSignal("Anchored"):Connect(function()
    print("anchored is now", part.Anchored)
end)
```

The signal carries **no value**. It says the property changed, and the handler
reads whatever it settled on — which matters, because three writes before one
drain produce three fires in write order, and all three handlers read the same
final value.

There is no catch-all `Changed` event. It cannot be typed under a fully strict
surface, and its removal is one of the deliberate divergences.

## Attributes

An attribute is your own named value on any instance, from a fixed value domain:
`string`, `number`, `boolean`, `vector`, `CFrame`, `Color3`, `Vector2`, `UDim`,
`UDim2` and `Rect`.

```luau
part:SetAttribute("Damage", 25)
part:SetAttribute("Respawns", true)

local damage = part:GetAttribute("Damage")
for name, value in part:GetAttributes() do
    print(name, value)
end
```

Setting an attribute to `nil` removes it. A change raises two signals in a fixed
order: `Instance.GetAttributeChangedSignal` for that name first, then the
class's `Instance.AttributeChanged` carrying the name. The narrower subscription
— the one that asked about *this* attribute — is answered before the router, and
routing after the specific fact has been observed is the order that composes.

Attributes survive serialization and `Instance.Clone`. Use them for
per-instance configuration somebody should be able to change without touching
code — and for per-script configuration, since `script` is an instance like any
other.

## Tags

A tag is a name an instance either carries or does not.

```luau
local TagService = game:GetService("TagService")

checkpoint:AddTag("Checkpoint")

for _, instance in TagService:GetTagged("Checkpoint") do
    -- every checkpoint in the world, right now
end

TagService:GetInstanceAddedSignal("Checkpoint"):Connect(function(instance: Instance)
    -- and every one that appears later, including from a streamed chunk
end)
```

`TagService.GetInstanceAddedSignal` is what makes tags worth having: a system
registers interest in a tag once, at boot, and then handles every instance that
ever carries it — authored in a scene, spawned by a script, or materialized by
streaming — without knowing where it came from or when.

`Instance.HasTag`, `Instance.GetTags`, `Instance.RemoveTag` and
`TagService.GetAllTags` complete the surface.

## Choosing

- The engine needs to act on it → it is a **property**, and you cannot add one.
- Your game needs a typed value on one instance → **attribute**.
- Your game needs to find a *set* of instances, or to react to them appearing →
  **tag**.

A name is not a key: siblings may share one. When you find yourself matching on
`Instance.Name` to identify a category, you wanted a tag.
