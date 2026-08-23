# Scenes: the world as data

A scene is a world written down as a file. The engine loads it at boot, and then
the scripts run.

> **The authored world is data. Scripts are behaviour.**

That split is what the visual editor authors against, and it is the arrangement
the large engines use. It does not deprecate building a world in code —
`Instance.new` at runtime stays first-class — but it moves where the world a
project *starts* with is written down.

## Naming one

```toml
[project]
scene = "scenes/main.scene.json"
```

The path is relative to the project's content directory. Absent or empty means
the project has no starting world, and whatever the scripts build is the world —
which is how every example before scenes existed still works.

## What a scene file is

JSON, with a format marker and a root instance carrying children:

```json
{
  "format": "luaug-scene",
  "version": 1,
  "root": {
    "class": "Workspace",
    "name": "Workspace",
    "properties": {
      "CurrentCamera": "Workspace.MainCamera"
    },
    "children": [
      {
        "class": "Camera",
        "name": "MainCamera",
        "properties": {
          "CFrame": [0.0, 6.0, 18.0, 1.0, 0.0, 0.0, 0.0, 0.97, -0.24, 0.0, 0.24, 0.97],
          "FieldOfView": 70.0
        }
      },
      {
        "class": "Part",
        "name": "Ground",
        "properties": {
          "Size": [40.0, 1.0, 40.0],
          "Color": [0.35, 0.42, 0.32],
          "Anchored": true
        }
      }
    ]
  }
}
```

A few things worth reading off that:

- **Classes are named by their class name**, and properties by their property
  name — the same names a script uses.
- **A `CFrame` is twelve numbers**: three of translation, then the basis.
- **An instance reference is a path**, resolved after the whole tree is built,
  so an instance can refer to one declared later.
- Tags and attributes travel with an instance too.

## Load order

The scene is applied **before the entry scripts start**. That order is the whole
point: a script that looks for what the scene declared finds it already there.

```luau
--!strict
local ground = workspace:FindFirstChild("Ground")
-- present at file scope, because the scene was built first
```

A hot reload rebuilds the world the same way, scene first, so a saved script
comes back into the world it was written against.

## What a script does with a scene

Behaviour, and nothing else. The idiomatic shape is a system that finds what it
cares about by tag and wires it, without knowing which of them came from the
file and which a script spawned:

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

## Editing one

`luaug edit <project>` opens the editor. A scene is one of the assets in the
content browser, so you open it there, click things in the viewport, change
properties, and save.

The loop that matters is that **pressing play does not cost you your work**: the
world runs, and stopping restores it to exactly where play was pressed, with
your edits still in it. A tool where testing your work costs you your work is
one nobody uses twice.

Save rewrites **the scene you have open**, not a fixed name — which is the
difference between a project with scenes and a project with a scene.

## Where to look next

- [The visual editor](manual:get-started/editor)
- [The world is data, scripts are behaviour](manual:why/world-is-data)
- [Prefabs](manual:world/prefabs)
