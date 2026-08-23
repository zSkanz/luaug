# The Instance tree

Everything in a LuauG world is an `Instance` in one tree, and the tree is the
only thing a script traverses. There is no scene graph beside it, no separate
"game object" list, and no hidden registry — if something is in the world, it is
reachable by walking down from `game`.

## The three globals

| Global | What it is |
|---|---|
| `game` | The `DataModel`: the root, and the only place services come from. |
| `workspace` | `game:GetService("Workspace")`, spelled shorter because it is used constantly. |
| `script` | The `Script` instance the running file was mounted as. |

They are globals rather than requires because they are the world, not a library
— the split is explained under [What a script may do](manual:concepts/sandbox).

## Making one

```luau
local part = Instance.new("Part")
part.Name = "Platform"
part.Size = vector.create(8, 1, 8)
part.Anchored = true
part.Parent = workspace
```

`Instance.new` takes **one argument**. There is no second `parent` parameter:
setting `Parent` last is the only spelling, and it is the fast one, because a
part that is parented before it is configured makes the engine react to every
intermediate value.

An abstract class and a service both refuse construction. `Instance.new("BasePart")`
raises because `BasePart` is abstract, and `Instance.new("Workspace")` raises
because a service is a singleton reached through [`DataModel.GetService`](api:DataModel.GetService).
The reference marks both: a page carrying the **Creatable** badge is one
`Instance.new` accepts.

## Finding things

```luau
local platform = workspace:FindFirstChild("Platform")
local firstLight = workspace:FindFirstChildOfClass("PointLight")
local anySolid = workspace:FindFirstChildWhichIsA("BasePart")
```

`FindFirstChildOfClass` matches the class **exactly**, so asking for `BasePart`
never finds a `Part`. `FindFirstChildWhichIsA` matches through the hierarchy, so
it does. That difference is the whole reason both exist.

`Instance.GetChildren` and `Instance.GetDescendants` each return a **fresh
array** that the caller owns, so destroying while iterating one is safe — the
array may simply contain instances that are already gone by the time you reach
them.

`Instance.WaitForChild` yields until a child of that name exists, with an
optional timeout. Renaming an existing child *into* the awaited name satisfies a
waiter exactly as parenting a new child does.

## Children are not members

`workspace.Baseplate` does not reach a child called `Baseplate`. It raises
`scene.err.unknown_member`, because members and children live in separate
namespaces. This is one of the deliberate divergences and it has a page of its
own: [Children are not members](manual:why/children-are-not-members).

Siblings may also share a name. `FindFirstChild` returns the first in document
order and nothing enforces uniqueness, so a name is a label rather than a key —
which is what `Instance.AddTag` and `TagService` are for when you need to find a
set.

## Order

Two orders matter and they are the same order seen twice.

- **Child order** is the order instances were parented, and it is what
  `GetChildren` returns.
- **Document order** is child order taken depth-first, preorder, and it is what
  `GetDescendants` returns and what every `FindFirst…` tie-breaks on.

Both are stable across runs. Nothing in the engine iterates an unordered
container into an observable order — see [Determinism](manual:concepts/determinism).

## Moving something

Assigning `Instance.Parent` appends the instance **last** among its new
siblings. Assigning the parent it already has changes nothing and raises no
signals.

One re-parent raises five kinds of signal in one fixed order — leaving is fully
observed before arriving, and the moved subtree is told last:

1. `Instance.ChildRemoved` on the old parent
2. `Instance.DescendantRemoving` on the old ancestors, nearest first
3. `Instance.ChildAdded` on the new parent
4. `Instance.DescendantAdded` on the new ancestors, nearest first
5. `Instance.AncestryChanged` on the moved instance, then on each descendant in
   document order

Steps 1–2 are absent when there was no old parent, and 3–4 when there is no new
one. All of them are deferred: see [Signals and connections](manual:concepts/signals).

## Destroying something

```luau
part:Destroy()
```

**The tree mutation is synchronous.** When `Instance.Destroy` returns, `Parent`
is already `nil`, the instance is already absent from its former parent's
`GetChildren`, and its children have been destroyed recursively. Only the
signals are deferred.

`Parent` then locks: assigning it afterwards raises `scene.err.parent_locked`.
A second `Destroy` is a no-op and does not fire `Instance.Destroying` twice.

**A destroyed handle stops resolving** at the end of the drain in which
`Destroying` fired. Until then the corpse is still usable; after it, every
access raises `script.err.instance_dead`. This is deliberate — the ECS reclaims
the slot, and a use-after-destroy becomes a keyed error instead of a silent read
of a corpse. Roblox leaves such a handle readable forever; LuauG does not.

## Copying a subtree

`Instance.Clone` deep-copies an instance with its children, properties,
attributes and tags, fixing up internal references. A tree kept unparented (or
under a storage `Folder`) and cloned on demand is the prefab pattern, and it is
one of three — see [Prefabs](manual:world/prefabs).

## Asking what something is

```luau
if instance:IsA("BasePart") then
    -- every Part, MeshPart and CharacterBody reaches here
end
```

`typeof(instance)` answers `"Instance"` whatever the class, so `Instance.ClassName`
is what tells two classes apart and `Instance.IsA` is what tests a family.
`Instance.IsDescendantOf` and `Instance.IsAncestorOf` answer the tree questions.

## Where to look next

- [Services](manual:concepts/services) — the singletons hanging off `game`
- [Properties, attributes and tags](manual:concepts/properties) — the three
  ways an instance carries data
- [The Instance reference](api:Instance) — every member, in full
