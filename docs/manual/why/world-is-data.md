# The world is data, scripts are behaviour

A project's starting world is a **file**. Scripts say what it does.

```text
content/scenes/main.scene.json     what is there
src/scripts/spin.luau              what it does
```

That is the arrangement the large engines use, and it is the one this engine
moved to.

## What it replaces

Building the world in code:

```luau
--!strict
-- Every example before scenes existed opened like this.
local ground = Instance.new("Part")
ground.Size = vector.create(40, 1, 40)
ground.Color = Color3.fromRGB(90, 108, 82)
ground.Anchored = true
ground.Parent = workspace
-- ...and two hundred more lines of the same
```

That works, and it is still supported — but it has three costs that grow with
the project:

- **Nobody can see it.** A world in code is a world you have to run to look at.
- **Nobody but a programmer can change it.** Moving a lamp is an edit to a
  source file.
- **A diff of it is a diff of code.** Changing one colour is a line in a
  function among lines that do something else.

## What it buys

**An editor is possible.** A visual editor needs something to author; if the
world is a program, the only honest editor is a text editor. This is the change
that makes clicking a thing and typing a new colour into it meaningful.

**The two halves diff separately.** A colour change is a change to the scene
file. A behaviour change is a change to a script. Reviewing either does not mean
reading the other.

**Behaviour composes over content it did not create.** The idiomatic script does
not name instances; it finds them by tag and wires them:

```luau
--!strict
local RunService = game:GetService("RunService")
local TagService = game:GetService("TagService")

local spinning: { BasePart } = {}

local function track(instance: Instance)
    if instance:IsA("BasePart") then
        table.insert(spinning, instance :: BasePart)
    end
end

for _, instance in TagService:GetTagged("Spin") do
    track(instance)
end
TagService:GetInstanceAddedSignal("Spin"):Connect(track)

RunService.Heartbeat:Connect(function(dt: number)
    for _, part in spinning do
        part.CFrame = part.CFrame * CFrame.fromEuler(0, dt, 0)
    end
end)
```

That script does not know how many things spin, where they are, or who made
them. It works for one the scene declared, one a script spawned, and one
streaming materialised.

## Code-first is not deprecated

`Instance.new` at runtime stays first-class, exactly as it is in every engine
that works this way. Spawning, pooling, procedural generation and everything
built at runtime are unchanged.

What moved is where the world a project **starts** with is written down. A game
with no scene is still a legal game; a game whose whole world is generated is
still a legal game.

## The load order that makes it work

The scene is applied **before the entry scripts start**. That single ordering is
what makes the pattern above viable: a script that looks for what the scene
declared finds it already there, at file scope, without waiting.

A hot reload rebuilds the world the same way — scene first — so a saved script
comes back into the world it was written against.

## What it asks of you

**Stop naming instances in code.** A script that does
`workspace:FindFirstChild("Lamp3")` is coupled to a layout somebody will change.
Tag it, and let the script find the set.

**Put configuration on the instance.** An attribute survives a scene save, a
clone and a reload, and is editable in the properties panel — which is where a
designer will look for it.

## Where to look next

- [Scenes: the world as data](manual:world/scenes)
- [The visual editor](manual:get-started/editor)
- [Properties, attributes and tags](manual:concepts/properties)
