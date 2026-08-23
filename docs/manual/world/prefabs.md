# Prefabs

A prefab is an instance tree authored once and instantiated many times. In this
release there is **one mechanism**, and it is `Instance.Clone`.

## A template is a tree nothing has parented

```luau
--!strict
local function makeCrateTemplate(): Model
    local crate = Instance.new("Model")
    crate.Name = "Crate"

    local body = Instance.new("Part")
    body.Size = vector.create(1.2, 1.2, 1.2)
    body.Color = Color3.fromRGB(150, 108, 62)
    body.Density = 0.4
    body:AddTag("Pickup")
    body.Parent = crate

    crate.PrimaryPart = body
    return crate                 -- deliberately unparented
end

local Template = makeCrateTemplate()
```

A tree with no parent is not in the world: nothing draws it, nothing simulates
it, nothing finds it in a tree walk. That is exactly what a template should be.

Keeping it under a storage `Folder` instead works the same way, and is easier to
inspect — a `Folder` parented to `game` rather than to `workspace` is out of the
scene.

## Instantiating

```luau
--!strict
local function spawnCrate(at: vector): Model
    local crate = Template:Clone()
    crate:PivotTo(CFrame.new(at))
    crate.Parent = workspace
    return crate
end
```

`Instance.Clone` deep-copies the instance with its children, its properties, its
attributes and its tags, and fixes up references that pointed inside the copied
tree — a `Model.PrimaryPart` naming a child comes out pointing at the *new*
child.

Configure the clone, then parent it. That order matters for the same reason it
matters for `Instance.new`.

## Parameterising

Because a template is code, a prefab takes arguments without needing a format
for them:

```luau
--!strict
local function spawnCrate(at: vector, contents: string): Model
    local crate = Template:Clone()
    crate:SetAttribute("Contents", contents)
    crate:PivotTo(CFrame.new(at))
    crate.Parent = workspace
    return crate
end
```

Attributes are the right home for that: they are typed, they survive a clone,
and a system that cares can find every crate by tag and read what is in it
without knowing who spawned it.

## Finding them again

```luau
--!strict
local TagService = game:GetService("TagService")

TagService:GetInstanceAddedSignal("Pickup"):Connect(function(instance: Instance)
    wirePickup(instance)
end)
```

A tag on the template is on every clone. Registering interest once, at boot,
then handling every instance that ever carries the tag, is the pattern that
makes prefabs and systems compose — see
[Properties, attributes and tags](manual:concepts/properties).

## Prefabs from files

A prefab authored as a **file** rather than as code — the thing a visual editor
would write when you say "save as prefab" — is not in this release. There is no
asset service and no prefab format a script can load.

What exists instead, and covers most of the same ground, is the **scene**: a
whole world authored as data and loaded at boot. See
[Scenes: the world as data](manual:world/scenes).

So the honest shape of things today:

| Want | Use |
|---|---|
| Many copies of one object, spawned at runtime | A template plus `Clone` |
| A whole authored world | A scene file |
| Imported art with a hierarchy | Several `MeshPart`s under a `Model` |

## Where to look next

- [Scenes: the world as data](manual:world/scenes)
- [The Instance tree](manual:concepts/instance-tree) — what `Clone` copies
- [`Instance.Clone`](api:Instance.Clone)
