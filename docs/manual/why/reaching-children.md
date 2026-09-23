# Reaching a child

```luau
--!strict
local walker = workspace.Player.Walker      -- a dot reaches a child
walker.WalkSpeed = 12                        -- and it is typed: Walker is a CharacterBody
local maybe = workspace:FindFirstChild("Crate")   -- may be nil, and you say so
local ready = workspace:WaitForChild("Crate")     -- I will wait, deliberately
```

## What a dot reads

A dot reads a **member** first: a property, a method or an event the class
declares. Only when there is none does it look for a **child** of that name,
and it returns the first one. So a part called `Name` never hides the `Name`
property, and a name that is neither is still an error that says so.

A dot does not assign to a child: `workspace.Crate = otherPart` is refused,
because a child is replaced by parenting another instance, not by assignment.

## How it is typed

Every project is written in strict mode, which is what makes `part.Anchorred =
true` an error before the game runs. A child's name is the project's, not the
engine's, so the engine cannot declare it. **The project declares it**, in
`.luaug/types/scene.d.luau`: the scene's own tree, written by the engine.

```luau
declare workspace: Workspace & {
    Player: Model & {
        Walker: CharacterBody,
    },
}
```

That file is rewritten when the editor opens the project, on every Save, and by
`luaug setup`. VS Code and `luaug check` load it after the engine's own
definitions. Then `workspace.Player.Walker` is a `CharacterBody`, and
`workspace.Playr` is still a mistake the analyzer catches.

## What is not typed

A child a script makes while the game runs has no name the scene knows. A dot
still reaches it at run time, but the analyzer cannot know it is there. Reach it
the explicit way:

| Want | Use |
|---|---|
| One child by name, maybe | `FindFirstChild` -- returns `Instance?`, and the type makes you handle `nil` |
| One child that will arrive | `WaitForChild` -- yields, and the code shows that it does |
| A child you just made | the variable you made it into |
| Every child | `GetChildren` |

## Where to look next

- [The instance tree](manual:concepts/instance-tree)
- [Scenes: the world as data](manual:world/scenes)
