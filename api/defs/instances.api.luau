--!strict
--- Instance classes (api-design.md §2.2).
---
--- The M2 surface only: `Instance` and the classes a script can build a tree
--- out of. Classes whose behaviour belongs to a later milestone arrive with it,
--- because a property that accepts a write and changes nothing is worse than a
--- missing one -- it type-checks.

local Schema = require("../schema")

--- The attribute value domain (§2.2). Written once because it widens as the
--- remaining datatypes ship, and three copies would widen at three different
--- times. It widened once, at M6, when `Vector2`, `UDim`, `UDim2` and `Rect`
--- were declared -- which is the whole of what §2.2's "as those datatypes ship"
--- was waiting for.
local AttributeValue = "string | number | boolean | vector | CFrame | Color3 | Vector2 | UDim | UDim2 | Rect"
local OptionalAttributeValue = "(" .. AttributeValue .. ")?"
local AttributeMap = "{ [string]: " .. AttributeValue .. " }"

--- Where a class does its job (`Schema.Class.Parents`), in the groups most of
--- them share. The two storages are in every one: a template waiting in
--- `ReplicatedStorage` to be cloned is exactly where anything may sit.
local function under(...: string): { string }
    local places = { ... }
    table.insert(places, "ReplicatedStorage")
    table.insert(places, "ServerStorage")
    return places
end
--- Something with a place in the 3D world: the world, a model, or a part
--- (a part inside a part moves with it).
local InTheWorld = under("Workspace", "Model", "BasePart")
--- Something a part or a point on one gives off or wears.
local OnAPart = under("BasePart", "Attachment")
--- An element of a UI tree, under one of its three roots or another element.
local InAUi = under("ScreenGui", "BillboardGui", "SurfaceGui", "UIObject")

--- The list carries its own annotation so that `Schema.Class` is the expected
--- type of every entry. Written inline under `Definition`, the new solver infers
--- the element type from the first class and then rejects every later one that
--- omits an optional field -- which is most of them.
local Classes: { Schema.Class } = {
    {
        Name = "Instance",
        Module = "scene",
        Tags = { "Abstract", "NotCreatable" } :: { Schema.ClassTag },
        Doc = "The root of the class hierarchy: a node in the tree, carrying a name, a parent, attributes and tags.",
        Properties = {
            {
                Name = "Name",
                Type = "string",
                Doc = "This instance's name; siblings may share one, and renaming into an awaited name satisfies a WaitForChild waiter exactly as parenting a new child does.",
            },
            {
                Name = "Parent",
                Type = "Instance?",
                Doc = "The instance this one hangs under, nil when unparented; assigning it appends this instance last among its new siblings, assigning the current parent again changes nothing, and assigning after Destroy raises.",
            },
            {
                Name = "ClassName",
                Type = "string",
                ReadOnly = true,
                Doc = "The name of this instance's class; typeof answers Instance whatever the class, so this is what tells two classes apart.",
            },
        },
        Methods = {
            {
                Name = "FindFirstChild",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { "Instance?" },
                Doc = "The first direct child with that name in document order, or nil; there is no recursive form in v1, so depth is a GetDescendants loop.",
            },
            {
                Name = "FindFirstChildOfClass",
                Parameters = { { Name = "className", Type = "string" } },
                Returns = { "Instance?" },
                Doc = "The first direct child whose ClassName is exactly className, or nil; asking for BasePart therefore never finds a Part.",
            },
            {
                Name = "FindFirstChildWhichIsA",
                Parameters = { { Name = "className", Type = "string" } },
                Returns = { "Instance?" },
                Doc = "The first direct child that IsA className, or nil; matching runs through the class hierarchy, so abstract base names are accepted.",
            },
            {
                Name = "FindFirstAncestor",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { "Instance?" },
                Doc = "The nearest ancestor with that name, searching upward from Parent; the instance itself is never a candidate.",
            },
            {
                Name = "FindFirstAncestorOfClass",
                Parameters = { { Name = "className", Type = "string" } },
                Returns = { "Instance?" },
                Doc = "The nearest ancestor whose ClassName is exactly className, searching upward from Parent; the instance itself is never a candidate.",
            },
            {
                Name = "GetChildren",
                Parameters = {},
                Returns = { "{Instance}" },
                Doc = "A fresh array of the direct children in child order, which is the order they were parented; the caller owns it, so destroying while iterating is safe.",
            },
            {
                Name = "GetDescendants",
                Parameters = {},
                Returns = { "{Instance}" },
                Doc = "A fresh array of every descendant in document order -- child order taken depth-first, preorder -- which is the order the Find family tie-breaks on.",
            },
            -- Parks the caller without the `Async` suffix §9 otherwise
            -- requires. The rule now carries a `Wait*` exception rather than
            -- the name carrying a suffix: `Async` exists to warn that a call
            -- parks when its name does not say so, and this one says so.
            {
                Name = "WaitForChild",
                Parameters = {
                    { Name = "name", Type = "string" },
                    { Name = "timeout", Type = "number?", Default = "nil" },
                },
                Returns = { "Instance?" },
                Yields = true,
                Doc = "Parks the caller until a child of that name exists here and returns it, returning immediately when one already does; with a timeout, expiry returns nil, and without one the wait is unbounded and warns after five sim-seconds.",
            },
            {
                Name = "IsA",
                Parameters = { { Name = "className", Type = "string" } },
                Returns = { "boolean" },
                Doc = "Whether this instance is of that class or descends from it; a string naming no class answers false rather than raising, because the point is to test names you do not trust.",
            },
            {
                Name = "IsAncestorOf",
                Parameters = { { Name = "descendant", Type = "Instance" } },
                Returns = { "boolean" },
                Doc = "Whether descendant sits anywhere below this instance; read strictly, so an instance is not its own ancestor.",
            },
            {
                Name = "IsDescendantOf",
                Parameters = { { Name = "ancestor", Type = "Instance" } },
                Returns = { "boolean" },
                Doc = "Whether this instance sits anywhere below ancestor; read strictly, so an instance is not its own descendant.",
            },
            {
                Name = "Clone",
                Parameters = {},
                Returns = { "Instance" },
                Doc = "Deep-copies this subtree unparented -- children, properties, attributes and tags at every depth -- rewiring object references that point inside the copy and leaving those that point outside it on the originals.",
            },
            {
                Name = "Destroy",
                Parameters = {},
                Returns = {},
                Doc = "Removes this instance and its children from the tree synchronously, clears its tags, locks Parent to nil and defers Destroying; the handle stops resolving at the end of that drain.",
            },
            {
                Name = "GetAttribute",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { OptionalAttributeValue },
                Doc = "The value of that attribute, or nil when it has never been set; attribute names are case-sensitive.",
            },
            {
                Name = "SetAttribute",
                Parameters = {
                    { Name = "name", Type = "string" },
                    { Name = "value", Type = OptionalAttributeValue },
                },
                Returns = {},
                Doc = "Sets that attribute, or removes it when value is nil; a table, an Instance or a function raises and leaves any previous value in place, and an empty name raises.",
            },
            {
                Name = "GetAttributes",
                Parameters = {},
                Returns = { AttributeMap },
                Doc = "A fresh table of this instance's attributes, which the caller owns.",
            },
            {
                Name = "GetAttributeChangedSignal",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { "Signal<()>" },
                Doc = "The signal fired when that attribute changes; any name is accepted, because an attribute that has never been set is a reasonable thing to wait for.",
            },
            {
                Name = "GetPropertyChangedSignal",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { "Signal<()>" },
                Doc = "The signal fired when that property changes -- the same object on every call, carrying no value so its type stays independent of the property's; a name the class does not have raises.",
            },
            {
                Name = "AddTag",
                Parameters = { { Name = "tag", Type = "string" } },
                Returns = {},
                Doc = "Adds a tag to this instance, a no-op when it is already present; tags are pure instance state, so a nil-parented instance still carries and is found by them.",
            },
            {
                Name = "RemoveTag",
                Parameters = { { Name = "tag", Type = "string" } },
                Returns = {},
                Doc = "Removes a tag from this instance, a no-op when it is absent.",
            },
            {
                Name = "HasTag",
                Parameters = { { Name = "tag", Type = "string" } },
                Returns = { "boolean" },
                Doc = "Whether this instance currently carries that tag.",
            },
            {
                Name = "GetTags",
                Parameters = {},
                Returns = { "{string}" },
                Doc = "A fresh array of this instance's tags, which the caller owns.",
            },
        },
        Events = {
            {
                Name = "ChildAdded",
                Parameters = { { Name = "child", Type = "Instance" } },
                Doc = "Fired after an instance is parented to this one, carrying that child.",
            },
            {
                Name = "ChildRemoved",
                Parameters = { { Name = "child", Type = "Instance" } },
                Doc = "Fired after an instance stops being parented to this one, carrying that child.",
            },
            {
                Name = "DescendantAdded",
                Parameters = { { Name = "descendant", Type = "Instance" } },
                Doc = "Fired after an instance is added anywhere below this one, carrying that descendant.",
            },
            {
                Name = "DescendantRemoving",
                Parameters = { { Name = "descendant", Type = "Instance" } },
                Doc = "Fired as an instance leaves the subtree below this one, carrying that descendant; leaving is fully observed before arriving.",
            },
            {
                Name = "AncestryChanged",
                Parameters = {
                    { Name = "instance", Type = "Instance" },
                    { Name = "newParent", Type = "Instance?" },
                },
                Doc = "Fired on a moved instance and then on each of its descendants in document order, carrying the instance whose ancestry changed and its new parent, nil when it was unparented.",
            },
            {
                Name = "AttributeChanged",
                Parameters = { { Name = "name", Type = "string" } },
                Doc = "Fired when any attribute of this instance changes, carrying the attribute name.",
            },
            {
                Name = "Destroying",
                Parameters = {},
                Doc = "Fired once when Destroy is called, carrying nothing; the tree mutation has already happened and only the signal is deferred.",
            },
        },
    },
    {
        Name = "PVInstance",
        Extends = "Instance",
        Module = "scene",
        Tags = { "Abstract", "NotCreatable" } :: { Schema.ClassTag },
        Storage = "PV",
        Doc = "The base of everything with a position and orientation in the world: a pivot to move it by, and "
            .. "an offset saying where that pivot sits. BasePart, Model and Camera are the three.",
        Properties = {
            {
                Name = "PivotOffset",
                Type = "CFrame",
                Doc = "Where this object's pivot sits relative to the object itself; the identity puts it at the "
                    .. "centre. Set it to a hinge edge and PivotTo rotates about that edge.",
            },
        },
        Methods = {
            {
                Name = "GetPivot",
                Parameters = {},
                Returns = { "CFrame" },
                Doc = "This object's pivot in world space.",
            },
            {
                Name = "PivotTo",
                Parameters = { { Name = "target", Type = "CFrame" } },
                Returns = {},
                Doc = "Moves this object so its pivot lands on target. A Model moves every part under it and keeps "
                    .. "their relative layout; a BasePart or Camera moves itself.",
            },
        },
    },
    {
        Name = "Folder",
        Extends = "Instance",
        Module = "scene",
        Doc = "A node with no behaviour of its own, for grouping instances; mounting src/scripts also builds one per subdirectory.",
    },
    {
        Name = "Model",
        Extends = "PVInstance",
        Module = "scene",
        Parents = InTheWorld,
        Storage = "Model",
        Doc = "A group of parts handled as one object, with a pivot to move it by and an extents box to measure it with.",
        Properties = {
            {
                Name = "PrimaryPart",
                Type = "BasePart?",
                Doc = "The part this model's pivot follows; Clone rewires it to the copy, since it is the reference into a cloned subtree that catches people.",
            },
            {
                Name = "StreamingMode",
                Type = "Enum.StreamingMode",
                Default = "Enum.StreamingMode.Nonatomic",
                Doc = "What this model does when the world is partitioned into a streaming grid (ADR 0053). It "
                    .. "is the one thing a person says about a world they built by hand: the grid decides WHEN "
                    .. "something becomes eligible, and this decides WHAT comes with it.\n\n**The default "
                    .. "changes nothing.** `Nonatomic` places each part in the cell its own position falls in, "
                    .. "which is how a world of loose scenery already behaves. `Atomic` makes the model one "
                    .. "unit that lives in a single cell however far it spreads, and `Persistent` keeps it out "
                    .. "of the grid entirely so it is always there.\n\nIt is read when the world is "
                    .. "partitioned, not every frame: changing it at run time changes nothing until the world "
                    .. "is built again.",
            },
            {
                Name = "Scale",
                Type = "number",
                Default = "1",
                Doc = "How big this model is relative to the size it was built at. Setting it resizes every "
                    .. "part under the model and moves each one about the model's pivot, so the whole thing "
                    .. "grows in place rather than drifting away from where it stood.\n\n**It is "
                    .. "absolute, not cumulative.** Setting 2 twice leaves the model twice its built size, "
                    .. "not four times -- the write scales by the ratio between the new value and the old "
                    .. "one. Setting 1 puts it back exactly.\n\n**A descendant added afterwards is "
                    .. "not scaled.** The write fans out once over what is there and then it is done: "
                    .. "nothing is multiplied by this per frame, which is what makes a scaled model cost "
                    .. "the same to draw and simulate as an unscaled one. Parent the part first, then "
                    .. "scale.\n\nZero and negative values are refused. A part with no size is not "
                    .. "a small part, it is a shape the solver cannot build.",
            },
        },
        Methods = {
            {
                Name = "GetExtentsSize",
                Parameters = {},
                Returns = { "vector" },
                Doc = "The size in metres of the smallest box enclosing every part in this model.",
            },
        },
    },
    {
        -- `NotCreatable` because dynamic script creation is not supported in
        -- v1 (§3): a Script exists because a file under `src/scripts` does.
        Name = "BaseScript",
        Extends = "Instance",
        Module = "scene",
        Tags = { "Abstract" } :: { Schema.ClassTag },
        -- `Script` and `ModuleScript` inherit this rather than declaring their
        -- own: `World::create` calls every hook down the ancestry, so a second
        -- one would attach a second `ScriptComponent`.
        Storage = "Script",
        Doc = "The abstract base of anything that carries Luau code. Its source is a PROPERTY, so a script is an "
            .. "ordinary instance: it is created like any other, it lives wherever you put it, and it is saved "
            .. "inside whatever holds it.",
        Properties = {
            {
                Name = "Source",
                Type = "string",
                Code = true,
                Default = '""',
                Doc = "The Luau this instance carries. It is data on the instance rather than a path to a file, "
                    .. "which is what makes a script something you can create, copy, put inside a prefab and save "
                    .. "in a scene like anything else.",
            },
        },
    },
    {
        Name = "Script",
        Extends = "BaseScript",
        Module = "scene",
        Doc = "Luau that RUNS. Every enabled Script in the world is started on its own coroutine when the world "
            .. "does, and one that is not in the world does not run -- which is the whole difference between "
            .. "storing a script and using it."
            .. '\n\n**In the editor, "when the world does" means when you press play** (ADR 0058). Opening a '
            .. "project mounts every script -- they are in the tree, `Source` is editable, a tab can open one -- and "
            .. "starts none of them, so a project shows what its scene holds and nothing a file scope built. Stop "
            .. "tears the VM down, which is what makes a second play the same as the first. Every other way of "
            .. "running this engine starts scripts at boot.",
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                -- The engine is the subscriber here. A write to this starts or
                -- stops a script (ADR 0059), and there is nothing for that to
                -- subscribe through -- the mask is per instance and set by a
                -- `GetPropertyChangedSignal`, so a rule that only worked once
                -- somebody had connected to a signal would usually not work.
                Observed = true,
                Doc = "**Whether this script's threads are resumed.** It decides nothing else "
                    .. "(ADR 0059).\n\nFalse at the moment scripts start means this one never "
                    .. "starts. Writing it "
                    .. "afterwards takes effect at the next deferred drain: **false to true STARTS it**, on its own "
                    .. "coroutine, file scope running now against the world as it is -- a start and not a resume, so "
                    .. "a script that ran, was disabled and is enabled again runs its file scope a second time. "
                    .. "**True to false stops resumption**, and that is all it does: a queued `task.defer`, a "
                    .. "`task.delay` coming due and a signal fire are all discarded when they come up, and a "
                    .. "thread already executing runs to its next yield, because a coroutine cannot be preempted."
                    .. "\n\n**Connections are NOT disconnected**, nothing it made is undone and nothing it "
                    .. "built is removed. They stop being invoked because nothing resumes the threads that would "
                    .. "run them; disconnect them yourself if that is what you need. Anything larger would answer "
                    .. "whether a connection dies with the thing its closure captured, which is a separate question "
                    .. "and not one a property setter may decide."
                    .. "\n\nSeveral scripts enabled by one tick start in document order.",
            },
        },
    },
    {
        Name = "ModuleScript",
        Extends = "BaseScript",
        Module = "scene",
        Doc = "Luau that is REQUIRED rather than run. It never starts on its own; `require(module)` evaluates it "
            .. "once and every later require of the same instance gives back the same value, which is what makes "
            .. "it the place shared code goes.",
    },
    {
        -- The structural half arrived at M2 and the physical half at M5, when
        -- there was a simulation to mean something by it. The surface is a
        -- material the part WEARS (ADR 0090): no colour of its own, only the
        -- parameters its material lets it override.
        Name = "BasePart",
        Extends = "PVInstance",
        Module = "scene",
        Tags = { "Abstract" } :: { Schema.ClassTag },
        -- `Part` inherits this rather than declaring its own: `World::create`
        -- calls every hook down the ancestry, so a second one would attach a
        -- second `PartComponent`.
        Storage = "Part",
        Doc = "The abstract base of every solid object in the world: a transform, a size and a look.",
        Properties = {
            {
                Name = "CFrame",
                Type = "CFrame",
                Doc = "This part's transform in world space, and the f64 source of truth Position and Orientation are read back from.",
            },
            {
                Name = "Position",
                Type = "vector",
                Doc = "This part's world position, the f32 rounding of the CFrame translation; gameplay math past a few kilometres from the origin belongs on CFrame instead.",
            },
            {
                Name = "Orientation",
                Type = "vector",
                Doc = "This part's world rotation as intrinsic YXZ euler angles in degrees.",
            },
            {
                Name = "Size",
                Type = "vector",
                Doc = "This part's dimensions in metres along its own axes.",
            },
            {
                Name = "Material",
                Type = "Material?",
                Doc = "The material this part wears: a handle to a material asset (`Material.load`), a runtime "
                    .. "copy of one (`material:Clone()`), or nothing -- which is the engine default material, "
                    .. "white, dielectric and fairly rough, and exactly what a plain part has always looked "
                    .. "like.\n\n**A part has no colour of its own** (ADR 0090). Its look is its material's, "
                    .. "and what it may change about that is what the material lets it: the parameters the "
                    .. "material declares, set with `SetMaterialParameter`. The engine default declares `Color` "
                    .. "and `Transparency`, so a part wearing nothing is still tinted and faded.\n\n**Reading "
                    .. "it never copies.** It returns the handle the part wears; `material:Clone()` is how to get "
                    .. "one to change. In a scene file it is written as the asset's `Content`.\n\nA `MeshPart` "
                    .. "with no material draws what its own file described, and a `Color` it overrides tints "
                    .. "that -- which is what an unimported mesh has always looked like.",
            },
            {
                Name = "MaterialParameters",
                Type = "MaterialParameters",
                Doc = "The parameters this part overrides, by name -- a fresh table on every read, holding only "
                    .. "what is overridden. The names are the closed set a material may declare: `Color`, "
                    .. "`Transparency`, `Emissive`, `Metalness`, `Roughness`, `NormalScale` and `AlphaCutoff`."
                    .. "\n\n**An override the current material does not declare is kept and ignored**, so a "
                    .. "part that changes material and changes back looks as it did. `SetMaterialParameter` is "
                    .. "the checked way to write one; assigning this property replaces the whole set and checks "
                    .. "only that every entry is a parameter with a value of its kind.",
            },
            {
                Name = "Anchored",
                Type = "boolean",
                Default = "false",
                Doc = "Whether the simulation moves this part. An anchored part is immovable scenery that other "
                    .. "parts collide with and gravity ignores -- not a very heavy part, a different kind of "
                    .. "body -- so writing this rebuilds it in the simulation and keeps its transform.",
            },
            {
                Name = "CanCollide",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this part physically stops anything. A part with this off still reports Touched, "
                    .. "which is what makes a checkpoint, a trigger volume or a pickup work: things pass through "
                    .. "it and it notices.",
            },
            {
                Name = "CanTouch",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this part takes part in touch reporting at all. **It gates the PAIR**: a part with "
                    .. "this off is silent, and so is whatever touches it -- because a signal naming a part that "
                    .. "said not to report touches would be that part reporting one on somebody else's handler. "
                    .. "That is the difference from CanQuery, which is about one part being found and is therefore "
                    .. "one part's decision. Off is what a world full of scenery wants: contacts are still solved, "
                    .. "and nothing is queued for scripts that were never going to listen.",
            },
            {
                Name = "CanQuery",
                Type = "boolean",
                Default = "true",
                Doc = "Whether raycasts, shapecasts and box queries can see this part. Independent of CanCollide, "
                    .. "because a part that blocks movement and a part a camera ray should ignore are different "
                    .. "questions.",
            },
            {
                Name = "CollisionGroup",
                Type = "string",
                Default = '"Default"',
                Doc = "The named group deciding which other parts this one collides with, registered through "
                    .. "`PhysicsService:RegisterCollisionGroup`. An unregistered name is an error rather than a "
                    .. "silent fallback to Default: the failure mode of a typo here is a wall players walk "
                    .. "through, which is expensive to find and cheap to refuse.",
            },
            {
                Name = "Friction",
                Type = "number",
                Default = "0.3",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How much this surface resists sliding, 0 frictionless and 1 very grippy. The value the "
                    .. "solver uses for a contact combines both surfaces' -- one slippery part is enough to make "
                    .. "a pair slide.",
            },
            {
                Name = "Restitution",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How much of an impact this surface gives back, 0 a dead stop and 1 a perfect bounce. "
                    .. "Combined across a contact pair the same way Friction is.",
            },
            {
                Name = "Density",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "Mass per cubic metre. Mass is this times the volume the Size and Shape describe, and there "
                    .. "is no Mass property precisely so that the two cannot be set to contradict each other.",
            },
            {
                Name = "LinearVelocity",
                Type = "vector",
                ReadOnly = true,
                Doc = "How fast this part is moving, in metres per second, as of the last simulation tick. "
                    .. "Read-only: a velocity assignment is an impulse with the mass divided out, and "
                    .. "`ApplyImpulse` is that operation under a name that says what it does.",
            },
            {
                Name = "AngularVelocity",
                Type = "vector",
                ReadOnly = true,
                Doc = "How fast this part is spinning, in radians per second about each world axis, as of the "
                    .. "last simulation tick.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "SetMaterialParameter",
                Parameters = { { Name = "name", Type = "string" }, { Name = "value", Type = "Color3 | number" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Overrides one parameter of the material this part wears, for this part alone. **Raises "
                    .. "for a parameter the material does not declare**: a material decides what a part may "
                    .. "change about it, and a tint written to one that did not allow it would be a surface "
                    .. "that silently ignores its script. `Color` and `Emissive` take a `Color3`; the rest take "
                    .. "a number.",
            },
            {
                Name = "GetMaterialParameter",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = { "Color3 | number" },
                ThreadSafety = "Unsafe",
                Doc = "The value this part draws with for one parameter: its override when it has one the "
                    .. "material declares, and the material's own value otherwise.",
            },
            {
                Name = "ClearMaterialParameter",
                Parameters = { { Name = "name", Type = "string" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Removes this part's override of one parameter, so it draws with the material's value "
                    .. "again. Clearing one that is not overridden does nothing.",
            },
            {
                Name = "SetNetworkOwner",
                Parameters = { { Name = "player", Type = "Player?" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Hands this part to one player's machine (ADR 0099), which then simulates it and sends "
                    .. "where it is: what that player pushes moves at once rather than a round trip later. "
                    .. "`nil` hands it back to the authority. Only the authority may call it, and only on a part "
                    .. "that is not anchored; a player who leaves gives back everything they owned. **What the "
                    .. "owner sends is trusted** -- a game that must not trust it checks it, or does not hand "
                    .. "the part over.",
            },
            {
                Name = "GetNetworkOwner",
                Parameters = {},
                Returns = { "Player?" },
                ThreadSafety = "ReadParallel",
                Doc = "The player whose machine simulates this part, or `nil` for the authority. A replica "
                    .. "knows only about itself: it answers its own `Player` for a part it owns, and `nil` "
                    .. "otherwise.",
            },
            {
                Name = "ApplyImpulse",
                Parameters = { { Name = "impulse", Type = "vector" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Adds an instantaneous change of momentum at the part's centre of mass, in kilogram-metres "
                    .. "per second. Applied at the next simulation tick and ignored by an anchored part, which "
                    .. "has no momentum to change.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "Touched",
                Parameters = { { Name = "otherPart", Type = "BasePart" } },
                Doc = "Fires once when another part begins touching this one, with the part it touched. Deferred "
                    .. "like every signal here (ADR 0015), so it arrives at the next resumption point rather than "
                    .. "inside the solver. A pair that stays in contact fires once, not once per tick -- including "
                    .. "across the moment the simulation puts both parts to sleep.",
            },
            {
                Name = "TouchEnded",
                Parameters = { { Name = "otherPart", Type = "BasePart" } },
                Doc = "Fires when a pair that was touching stops. Destroying a part does NOT fire it for that "
                    .. "part's contacts: the instance is gone, and a signal on an instance nobody can reach is a "
                    .. "signal nobody can handle.",
            },
        } :: { Schema.Event },
    },
    {
        Name = "Part",
        Extends = "BasePart",
        Module = "scene",
        Parents = InTheWorld,
        Doc = "The primitive solid: a part whose geometry is one of a small set of shapes rather than an imported mesh.",
        Properties = {
            {
                Name = "Shape",
                Type = "Enum.PartShape",
                Doc = "Which primitive solid this part is. It drives BOTH the geometry that is drawn and the "
                    .. "collider that is simulated, which is the point: a `Ball` that rolled like a box would be "
                    .. "the kind of divergence nobody can see in a screenshot.\n\n**`Size` is the full extent, "
                    .. "and two shapes read it slightly differently so that the mesh agrees with the collider.** "
                    .. "A `Ball` is a sphere of the LARGEST half-extent, so a non-uniform `Size` gives a ball "
                    .. "that sticks out of its own box rather than an ellipsoid you cannot collide with. A "
                    .. "`Cylinder` and a `Capsule` stand along Y, take their diameter from the larger of X and Z, "
                    .. "and take their full height from Y -- caps included. A capsule's caps are hemispheres the "
                    .. "collider never stretches, so `Size.y == 2 * Size.x` is where the drawn shape and the "
                    .. "simulated one agree exactly; away from it the drawn ends are slightly oval.\n\nA `Wedge` "
                    .. "is a ramp with its tall face towards -Z, so an unrotated one is walked UP as you walk "
                    .. "towards -Z. Its collider is deliberately the whole box (§2.2's physics notes).",
            },
        },
    },
    {
        Name = "MeshPart",
        Extends = "BasePart",
        Module = "render",
        Parents = InTheWorld,
        Storage = "MeshPart",
        Doc = "A part whose geometry is an imported mesh rather than a primitive solid. One file is one mesh: a "
            .. "model made of several pieces is several MeshParts, which is the shape a prefab assumes.",
        Properties = {
            {
                Name = "MeshContent",
                Type = "Content",
                ContentKind = "Mesh",
                Doc = "The glTF file this part renders. Setting it replaces the geometry; a file that fails to "
                    .. "import leaves the previous mesh in place and reports why, because a part that silently "
                    .. "becomes invisible is harder to diagnose than one that says it could not load.",
            },
            {
                Name = "CollisionFidelity",
                Type = "Enum.CollisionFidelity",
                Doc = "How closely the collision shape follows the rendered geometry.\n\n`Box` names a "
                    .. "shape rather than an accuracy and is honoured exactly: a caller who asked for the "
                    .. "bounding box gets it even when the mesh's geometry is loaded. Every other value asks "
                    .. "for the geometry, and what it gets is a CONVEX HULL -- a hull is what a rigid-body "
                    .. "solver can use directly, and a concave triangle mesh is a different shape class with "
                    .. "different rules (it cannot be dynamic) that nothing in v1 asks for.\n\nA mesh "
                    .. "whose points have not arrived yet collides as its bounding box, which is the state of "
                    .. "every `MeshPart` in the frame before its file finishes loading: a body with no shape "
                    .. "for one frame is a body that falls through the floor. So does a mesh too degenerate "
                    .. "to make a solid from -- three points are a triangle, not a hull.\n\nThe "
                    .. "property reads back what was written rather than what it resolved to, because a "
                    .. "value that silently became another value is worse than one that says what it did.",
            },
            {
                Name = "MeshSize",
                Type = "vector",
                Default = "vector.create(1, 1, 1)",
                Doc = "What the mesh measures at its authored size, in metres. `Size` divided by this is what "
                    .. "the part is scaled by, so setting `Size` to twice `MeshSize` draws the mesh at twice "
                    .. "its authored size and collides with a hull to match -- which is what makes `Size` mean "
                    .. "the same thing here as it does on a `Part`.\n\n**The import writes it, and it "
                    .. "is saved with the scene.** It is not read back off the file: the mesh's real bounds "
                    .. "are known only where something loaded it, and a scene has to describe the same world "
                    .. "whether or not anything is rendering. A part whose `Size` and `MeshSize` are both one "
                    .. "draws exactly as it always has.\n\nWriting it yourself stretches the mesh, "
                    .. "which is a legitimate thing to want; zero and negative components are refused, since "
                    .. "they are a division rather than a size.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "Bone",
        Extends = "Attachment",
        Module = "render",
        Parents = under("BasePart", "Bone"),
        -- No `Storage`: it inherits `Attachment`'s. `World::create` calls every
        -- hook down the ancestry, so declaring one here would attach a second
        -- `AttachmentComponent` -- the same reason `Script` does not declare one
        -- beside `BaseScript`'s.
        Doc = "An attachment that follows a joint of the `MeshPart` it is parented to. Weld a sword to a "
            .. "character's hand, hang a light off a lamp's swinging arm, or read where a foot is for a "
            .. "footstep -- and it moves with the animation, because it IS the joint.\n\n"
            .. "**Created on demand rather than one per joint.** A rig has tens of bones and a crowd has "
            .. "thousands; instantiating every one would put them all in the world hash and the change queue "
            .. "to answer a question about three of them. Make the ones you need.\n\nA "
            .. "bone whose `JointName` the rig does not have follows the mesh part itself, which puts it on "
            .. "the character rather than at the world origin -- the failure a renamed joint should produce "
            .. "is a sword in the wrong place, not a sword in another country.",
        Properties = {
            {
                Name = "JointName",
                Type = "string",
                Doc = "The joint in the parent `MeshPart`'s skeleton this follows, as the file names it.",
            },
            {
                Name = "JointIndex",
                Type = "number",
                ReadOnly = true,
                Default = "-1",
                Doc = "Where that name resolved to, or -1 for a name the rig does not have. Re-resolved when "
                    .. "the mesh reloads, so an index survives a file changing under it.",
            },
            {
                Name = "Transform",
                Type = "CFrame",
                Doc = "An offset applied on top of the animated pose, in the joint's own space.\n\n"
                    .. "This is the writable half of procedural animation: bend an arm towards a target, turn "
                    .. "a head to look at something, kick a gun up on a shot. It composes with whatever the "
                    .. "clip is doing rather than replacing it, and the identity is a bone nobody drives.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "Attachment",
        Extends = "Instance",
        Module = "scene",
        Parents = under("BasePart"),
        Storage = "Attachment",
        Doc = "A named place on a part: a socket to weld something to, a joint frame for a constraint, a "
            .. "muzzle to spawn something at. It has no body and is not simulated -- what it costs is one "
            .. "transform per tick, and a world with none pays nothing.\n\nIt is not on "
            .. "`PVInstance`, deliberately: an attachment is a thing you make and name, and giving every part "
            .. 'an invisible one would make "where is this attached" ambiguous in exactly the cases it '
            .. "needs to be clear.",
        Properties = {
            {
                Name = "CFrame",
                Type = "CFrame",
                Doc = "Where this sits relative to the part it is parented to.",
            },
            {
                Name = "WorldCFrame",
                Type = "CFrame",
                ReadOnly = true,
                Doc = "Where that lands in the world, recomputed each tick.\n\nRead-only "
                    .. "because a world transform that could be assigned would be a second source of truth "
                    .. "about the same place: the part moves, and whatever was written here would be either "
                    .. "overwritten or wrong.",
            },
        },
    },
    {
        -- `NotCreatable` and abstract: a constraint's TYPE is its class, because
        -- a hinge that could become a slider is two joints wearing one name.
        Name = "Constraint",
        Extends = "Instance",
        Module = "scene",
        Parents = InTheWorld,
        Storage = "Constraint",
        Tags = { "Abstract", "NotCreatable" } :: { Schema.ClassTag },
        Doc = "The base of anything the SOLVER holds together. A constraint hands both bodies to the solver "
            .. "and lets it work out where they end up -- a door swings under its own weight, a ragdoll "
            .. "falls.\n\n**This is not a `Weld`.** A weld DRIVES its second part from its "
            .. "first every tick and the solver is never asked, which is what keeps a sword on a hand "
            .. "whatever else is happening. A constraint asks. Use a weld to attach, a constraint to "
            .. "articulate.\n\n**It joins two `Attachment`s, not two parts**, which is "
            .. "what makes the joint frame something you can see and move: where a door hinges is a place "
            .. "on the door and a place on the frame. A constraint cannot reach a `CharacterBody` -- that is "
            .. "swept rather than solved, so there is no body for a joint to hold.",
        Properties = {
            {
                Name = "Attachment0",
                Type = "Attachment?",
                Doc = "One end of the joint. The part it is on is the body the solver holds.",
            },
            {
                Name = "Attachment1",
                Type = "Attachment?",
                Doc = "The other end. The two frames are the same place when the constraint is built, which "
                    .. 'is what "this is where they are attached" means.',
            },
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether the joint holds. A disabled constraint stays in the world holding nothing, "
                    .. "rather than being destroyed and rebuilt -- rebuilding would move it to the end of the "
                    .. "solve order, and a ragdoll that switched itself off and on would simulate differently "
                    .. "afterwards.",
            },
            {
                Name = "CollideConnected",
                Type = "boolean",
                Default = "true",
                Doc = "Whether the two parts still collide with each other.\n\n**False is "
                    .. "what a ragdoll needs.** An upper arm and a lower arm overlap at the elbow by "
                    .. "construction, and left colliding they shove each other apart every step -- a "
                    .. "character that vibrates rather than falls.",
            },
        },
    },
    {
        Name = "BallSocketConstraint",
        Extends = "Constraint",
        Module = "scene",
        -- A hook of its own, which does NOT add a second component: it stamps
        -- the kind onto the one `Constraint` already added. `World::create`
        -- walks the ancestry root-first, so the base has run by the time this
        -- does -- which is what lets the type be the class rather than a
        -- property somebody could write a hinge into.
        Storage = "BallSocketConstraint",
        Doc = "A joint free to rotate in every direction about one point, and optionally limited to a cone "
            .. "and a twist.\n\nUnlimited it is a rope's end or a pendulum. Limited it is a "
            .. "shoulder or a hip, which is exactly what a ragdoll is made of -- and the limited form is a "
            .. "different solver joint rather than a free one with corrections bolted on, so it is not "
            .. "exceeded and then pulled back, it is not exceeded.",
        Properties = {
            {
                Name = "LimitsEnabled",
                Type = "boolean",
                Default = "false",
                Doc = "Whether the cone and twist below apply. Off is a free ball joint.",
            },
            {
                Name = "UpperAngle",
                Type = "number",
                Default = "180",
                Doc = "The half-angle of the swing cone in degrees, measured from the joint's own X axis.",
            },
            {
                Name = "TwistLimit",
                Type = "number",
                Default = "180",
                Doc = "How far it may twist about that axis, in degrees, either way.",
            },
        },
    },
    {
        Name = "HingeConstraint",
        Extends = "Constraint",
        Module = "scene",
        -- A hook of its own, which does NOT add a second component: it stamps
        -- the kind onto the one `Constraint` already added. `World::create`
        -- walks the ancestry root-first, so the base has run by the time this
        -- does -- which is what lets the type be the class rather than a
        -- property somebody could write a hinge into.
        Storage = "HingeConstraint",
        Doc = "A joint free to rotate about one axis: a door, a lid, an elbow. The axis is the joint frame's "
            .. "own X, so pointing the attachment points the hinge.",
        Properties = {
            {
                Name = "LimitsEnabled",
                Type = "boolean",
                Default = "false",
                Doc = "Whether the range below applies. Off is a hinge that spins freely.",
            },
            {
                Name = "LowerAngle",
                Type = "number",
                Default = "-180",
                Doc = "How far it may turn one way, in degrees.",
            },
            {
                Name = "UpperAngle",
                Type = "number",
                Default = "180",
                Doc = "How far it may turn the other, in degrees. A lower above the upper is unlimited.",
            },
        },
    },
    {
        Name = "FixedConstraint",
        Extends = "Constraint",
        Module = "scene",
        -- A hook of its own, which does NOT add a second component: it stamps
        -- the kind onto the one `Constraint` already added. `World::create`
        -- walks the ancestry root-first, so the base has run by the time this
        -- does -- which is what lets the type be the class rather than a
        -- property somebody could write a hinge into.
        Storage = "FixedConstraint",
        Doc = "A joint with no freedom at all: two bodies the solver treats as one rigid assembly.\n\n"
            .. "**This is the one a `Weld` is not.** A weld moves its second part and asks nothing; this "
            .. "hands both to the solver, so a heavy crate fixed to a light one makes a heavy pair and a "
            .. "force applied to either moves both. Use a weld to attach something to a character, and this "
            .. "to build an object out of parts.",
        Properties = {},
    },
    {
        Name = "Ragdoll",
        Extends = "Instance",
        Module = "scene",
        Parents = under("MeshPart"),
        Storage = "Ragdoll",
        Doc = "Makes a character's pose come from the simulation instead of from a clip. Parent it to the "
            .. "`MeshPart` whose skeleton, put the limbs under it, and switch it on.\n\n"
            .. "**It owns nothing.** A ragdoll is parts, `Bone`s and constraints -- every one of them an "
            .. "instance you can see, select and move -- and this is the flag that says to drive the pose "
            .. "from them. A class that owned its own hidden bodies would be a second owner of things the "
            .. "physics mirror already creates from the tree, and two owners of one body is a rule "
            .. "broken.\n\nHow the pose is found: every `Bone` under this ragdoll that names "
            .. "a joint, sitting on a part. The part is where the simulation put that limb and the bone says "
            .. "which joint it is. Nothing else is declared, which is also why a partial ragdoll works -- "
            .. "simulate a dozen bones and the fingers ride along on the wrist, because the joints nobody "
            .. "drives keep their own place relative to their parent.",
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "false",
                Doc = "Whether the pose comes from the simulation.\n\nSwitching it off hands "
                    .. "the character back to whatever clip is playing, from wherever it is standing -- the "
                    .. "overrides stop being written and the next tick's pose is the animation's again.",
            },
            {
                Name = "Blend",
                Type = "number",
                Default = "1",
                Doc = "How much of the pose comes from the simulation: 1 is all of it, 0 is none. Clamped.\n\n"
                    .. "**A POSE blend, not a solver one**, and the difference matters. This interpolates each "
                    .. "driven joint between where the clip put it and where the limb ended up, which is what a "
                    .. "stumble is: the character keeps running and sags. It does not make the simulation "
                    .. "weaker -- the parts fall exactly as hard either way -- so a limb at 0.5 is drawn "
                    .. "halfway to a body that is somewhere else entirely, and far from the pose it started in "
                    .. "that reads as rubber. Driving the SOLVER towards a pose instead is a motorised joint, "
                    .. "and it is a different feature.\n\nRamping this from 0 to 1 over a few tenths of a "
                    .. "second is how a character goes down without the frame it changes on being a visible "
                    .. "snap, and ramping it back is how one gets up.",
            },
        },
        Methods = {
            {
                Name = "Build",
                Parameters = { { Name = "profile", Type = "{ any }" } },
                Returns = { "number" },
                Doc = "Creates the limbs: one `Part` per joint the profile names, a `Bone` on each saying which "
                    .. "joint it is, and a constraint holding it to the limb above. Returns how many limbs it "
                    .. "made.\n\n**It creates ordinary instances and nothing else**, which is the whole shape of "
                    .. "this class: what `Build` saves you is the arithmetic -- where each limb goes, how long it "
                    .. "is, which way it points -- and everything it produces is a thing you can select, move, "
                    .. "retune and delete. Run it once in the editor and save the scene, and the ragdoll is part "
                    .. "of the prefab with no script involved.\n\nThe limbs are placed at the rig's CURRENT "
                    .. "pose, so a character that goes down mid-stride falls from where it was standing.\n\n"
                    .. 'A profile is an ordered list of limbs; `require("@luaug/ragdoll").Humanoid` is a '
                    .. "built-in one that fits the common humanoid rigs. Each entry is a table:\n\n"
                    .. "- `Joint`: the joint's name, or a list of names tried in order -- the same shoulder is "
                    .. "`mixamorig:LeftArm` or `upper_arm.L` depending on who exported it.\n"
                    .. "- `Parent`: the 1-based index of the limb this hangs from, absent for the root. It must "
                    .. "point at a limb declared BEFORE this one.\n"
                    .. "- `Radius`, `LeafLength`: the capsule's radius, and the length to use for a limb with "
                    .. "nothing below it. A limb's length is otherwise the distance to the joint under it.\n"
                    .. '- `Kind`: `"BallSocket"`, `"Hinge"` or `"Fixed"`.\n'
                    .. "- `Swing`, `Twist`: a ball socket's cone half-angle and twist range, in degrees. Present "
                    .. "means it becomes a swing-twist, which is what a shoulder is.\n"
                    .. "- `LimitLow`, `LimitHigh`: a hinge's range in degrees.\n\n"
                    .. "**A limb the rig has no joint for is skipped, not refused**, and its children hang from "
                    .. "the nearest limb that was built -- so one humanoid profile works on a rig with no toes. "
                    .. "It errors when the ragdoll is not on a rigged MeshPart, when it already has limbs under "
                    .. "it, when the profile is malformed, or when the rig shares no joint with it at all.",
            },
        },
    },
    {
        Name = "Weld",
        Extends = "Instance",
        Module = "scene",
        Parents = InTheWorld,
        Storage = "Weld",
        Doc = "Holds one part at a fixed offset from another. **A transform weld, not a solver constraint**: "
            .. "Part1 stops being independently simulated and is driven from Part0 every tick, so welding a "
            .. "MeshPart to a CharacterBody keeps the two together without anything asking the solver's "
            .. "permission. Welding two dynamic parts so the SOLVER treats them as one rigid assembly is a "
            .. "different feature and is not this one. A weld whose two parts are already joined by another "
            .. "weld, directly or through a chain, is refused: welds form a graph and a cycle has no "
            .. "resolution order.",
        Properties = {
            {
                Name = "Part0",
                Type = "Instance?",
                Doc = "The anchor: the thing the other one follows. It may itself be simulated, anchored, or a "
                    .. "CharacterBody -- whatever moves it, Part1 goes with it.\n\n"
                    .. "**A `BasePart` or an `Attachment`.** Naming an attachment is what welds a sword to a "
                    .. "character's hand: the attachment is a `Bone`, the bone follows a joint, and the sword "
                    .. "follows the bone. Anything else is refused.",
            },
            {
                Name = "Part1",
                Type = "BasePart?",
                Doc = "The driven part. While the weld is active it is moved by the weld and by nothing else: "
                    .. "gravity does not reach it, and writing its CFrame is overwritten at the next tick.\n\n"
                    .. "**A part, never an attachment**, unlike `Part0`: a weld MOVES its driven end, and an "
                    .. "attachment has nothing of its own to move -- it is a place ON a part. Naming one here "
                    .. "would be a write that silently did nothing every tick.",
            },
            {
                Name = "C0",
                Type = "CFrame",
                Doc = "The attachment on Part0. The weld holds `Part0.CFrame * C0` equal to `Part1.CFrame * C1`, "
                    .. "which is the one sentence that says where both offsets go.",
            },
            {
                Name = "C1",
                Type = "CFrame",
                Doc = "The attachment on Part1, in the same relationship.",
            },
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether the weld holds. Disabling it hands Part1 back to the simulation where it stands, "
                    .. "with no velocity -- it is released rather than thrown.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "Terrain",
        Extends = "Instance",
        Module = "scene",
        Parents = under("Workspace"),
        Storage = "Terrain",
        Doc = "A sculpted, collidable landscape: ground you dig into rather than a floor made of parts.\n\n"
            .. "**It is a grid of voxels.** Space is cut into cubes `VoxelSize` on a side, and each one "
            .. "holds a material and an occupancy -- how full of that material it is, from 0 to 1. The "
            .. "surface is wherever the occupancy crosses one half, found by interpolating between "
            .. "neighbouring voxels, so a ball carved out of a hillside lands where you put it rather than "
            .. "on a grid line. Caves, arches, overhangs and flat ground are all the same data.\n\n"
            .. "Materials are numbered: 0 is air, and 1 to 8 are Grass, Sand, Rock, Snow, Mud, Sandstone, "
            .. "Basalt and Ice.\n\n"
            .. "There is one per `Workspace`, reached as `workspace.Terrain`, because a world has one "
            .. "ground. Creating a second is legal and it simply is not the one the workspace names.\n\n"
            .. "**Every verb here is a write to the voxels**, and the voxels are part of the world -- so a "
            .. "sculpt is undoable in the editor, it moves the world hash, and it saves with the project.",
        Properties = {
            {
                Name = "VoxelSize",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.terrain_not_empty",
                Doc = "How big one voxel is, in metres. One metre resolves a doorway and a path; two metres "
                    .. "resolves a hillside and costs an eighth as much.\n\n"
                    .. "**Only settable while the terrain is empty.** Changing it under a sculpted world "
                    .. "would mean resampling every voxel onto a different grid, which is a lossy operation "
                    .. "nobody asked for -- so it is refused, by name, rather than done quietly. Clear it "
                    .. "first if that is really what you want.",
            },
            {
                Name = "CellSize",
                Type = "number",
                ReadOnly = true,
                Default = "64",
                Inert = true,
                Doc = "How wide one streamed cell of terrain is, in metres. Read-only: it is the streaming "
                    .. "grid's own spacing, and a terrain that disagreed with it would have cells that "
                    .. "load and unload on different boundaries from everything else in the world.\n\n"
                    .. "**What a saved terrain streams by.** A terrain written with a scene and large "
                    .. "enough to span sixteen cells or more is cut into cells of this size, rounded to "
                    .. "whole columns of 32-voxel chunks, and each one is loaded and dropped around "
                    .. "`StreamingService`'s foci on its terrain radii. A cell somebody changed is never "
                    .. "dropped.",
            },
            {
                Name = "Position",
                Type = "vector",
                Default = "vector.zero",
                Doc = "Where this terrain's own origin sits in the world, in metres.\n\n"
                    .. "**A terrain is an instance and it can be moved.** Every voxel, every collider and "
                    .. "every brush stroke is offset by this rather than baked into the grid, so moving a "
                    .. "sculpted world is one number rather than a rewrite of every voxel. It translates "
                    .. "and does not turn.",
            },
            {
                Name = "MinHeight",
                Type = "number",
                Default = "-256",
                Doc = "The world's floor, in metres: no voxel below it is ever written.\n\n"
                    .. "Ground laid where there was none -- by `WriteHeights`, Generate Flat Ground or a "
                    .. "first raise on empty terrain -- is a slab 32 metres deep under its lowest surface, "
                    .. "rounded down to a whole chunk, and never deeper than this.",
            },
            {
                Name = "MaxHeight",
                Type = "number",
                Default = "256",
                Doc = "The world's ceiling, in metres: no voxel above it is ever written.",
            },
            {
                Name = "CellCount",
                Type = "number",
                ReadOnly = true,
                Doc = "How many chunks of 32 by 32 by 32 voxels currently hold anything. Air is never "
                    .. "stored, so zero is a terrain nobody has sculpted, which is what `Clear` returns it to.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "FillBall",
                Parameters = {
                    { Name = "center", Type = "vector" },
                    { Name = "radius", Type = "number" },
                    { Name = "material", Type = "number" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "Adds a ball of ground, or removes one when `material` is zero. Returns how many "
                    .. "voxels it changed.\n\n"
                    .. "Adding never takes ground away and removing never adds it: a voxel ends up as full "
                    .. "as the fuller of it and the ball (adding), or as empty as the ball leaves it "
                    .. "(removing). An explosion crater is `FillBall(hit.Position, 4, 0)`.",
            },
            {
                Name = "FillBlock",
                Parameters = {
                    { Name = "center", Type = "vector" },
                    { Name = "size", Type = "vector" },
                    { Name = "material", Type = "number" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "The same, as an axis-aligned box `size` across. Returns how many voxels it changed.",
            },
            {
                Name = "FillCylinder",
                Parameters = {
                    { Name = "center", Type = "vector" },
                    { Name = "height", Type = "number" },
                    { Name = "radius", Type = "number" },
                    { Name = "material", Type = "number" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "The same, as an upright cylinder `height` tall. Returns how many voxels it changed. "
                    .. "A well is `FillCylinder(top - vector.create(0, 10, 0), 20, 1.5, 0)`.",
            },
            {
                Name = "RaiseBall",
                Parameters = {
                    { Name = "center", Type = "vector" },
                    { Name = "radius", Type = "number" },
                    { Name = "amount", Type = "number" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "Raises the ground around `center` by `amount` metres at the middle, falling smoothly "
                    .. "to nothing at `radius` -- or lowers it, when `amount` is negative. Returns how many "
                    .. "voxels it changed.\n\n"
                    .. "**It moves the surface rather than adding a ball**: the ground within `radius` "
                    .. "above and below `center` is pushed up, so a hill rises without an overhang at its "
                    .. "rim, and a tunnel further down stays where it is. This is the verb for shaping hills "
                    .. "and valleys; `FillBall` is the one for a boulder or a tunnel.",
            },
            {
                Name = "SmoothBall",
                Parameters = {
                    { Name = "center", Type = "vector" },
                    { Name = "radius", Type = "number" },
                    { Name = "strength", Type = "number?" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "Softens the ground in a ball: every voxel moves towards the average of its neighbours "
                    .. "by `strength` (0 to 1, default 0.5), less towards the rim. Returns how many voxels "
                    .. "it changed.",
            },
            {
                Name = "FlattenBall",
                Parameters = {
                    { Name = "center", Type = "vector" },
                    { Name = "radius", Type = "number" },
                    { Name = "height", Type = "number" },
                    { Name = "strength", Type = "number?" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "Pulls the ground in a ball towards a level plane at `height` -- taking away what is "
                    .. "above it and filling what is below it -- by `strength` (0 to 1, default 1), less "
                    .. "towards the rim. Returns how many voxels it changed.",
            },
            {
                Name = "PaintBall",
                Parameters = {
                    { Name = "center", Type = "vector" },
                    { Name = "radius", Type = "number" },
                    { Name = "material", Type = "number" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "Changes what the ground is MADE OF in a ball, without moving it. Returns how many "
                    .. "voxels it changed.\n\n"
                    .. "A `material` of zero is refused rather than treated as erase. Zero means erase "
                    .. "to `FillBall`, and picking the first entry of a material list must not delete "
                    .. "the ground you were about to paint.",
            },
            {
                Name = "ReplaceMaterial",
                Parameters = {
                    { Name = "minCorner", Type = "vector" },
                    { Name = "maxCorner", Type = "vector" },
                    { Name = "from", Type = "number" },
                    { Name = "to", Type = "number" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "Every voxel of material `from` in the box between the two corners becomes `to`. "
                    .. "Returns how many it changed. Zero for either is refused: this changes what ground is "
                    .. "made of, never whether it is there.",
            },
            {
                Name = "ReadVoxels",
                Parameters = {
                    { Name = "minCorner", Type = "vector" },
                    { Name = "maxCorner", Type = "vector" },
                },
                Returns = { "{ number }", "{ number }", "vector" },
                ThreadSafety = "Unsafe",
                Doc = "Reads every voxel the box between the two corners touches, snapped outward to the "
                    .. "grid. Returns their materials, their occupancies (0 to 1) and how many there are on "
                    .. "each axis.\n\n"
                    .. "**The tables are flat**: the voxel at `x, y, z` (counting from 0 at the low corner) "
                    .. "is at index `1 + x + size.X * (y + size.Y * z)`. At most 256 cubed voxels at once; "
                    .. "read a larger region in pieces.",
            },
            {
                Name = "WriteVoxels",
                Parameters = {
                    { Name = "corner", Type = "vector" },
                    { Name = "size", Type = "vector" },
                    { Name = "materials", Type = "{ number }" },
                    { Name = "occupancies", Type = "{ number }" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "Writes a box of voxels in one call, laid out as `ReadVoxels` returns them: `corner` "
                    .. "is a point in the first voxel, `size` how many voxels on each axis, and both tables "
                    .. "hold one entry per voxel. Returns how many it changed.\n\n"
                    .. "A material of zero, or an occupancy of zero, is air. What `ReadVoxels` reads, "
                    .. "`WriteVoxels` puts back exactly -- the verb for copying a piece of terrain, "
                    .. "generating one voxel by voxel, or undoing a script's own change.",
            },
            {
                Name = "WorldToCell",
                Parameters = {
                    { Name = "position", Type = "vector" },
                },
                Returns = { "vector" },
                ThreadSafety = "Unsafe",
                Doc = "Which voxel a world position falls in, as its whole-number index on each axis.",
            },
            {
                Name = "CellCenterToWorld",
                Parameters = {
                    { Name = "cell", Type = "vector" },
                },
                Returns = { "vector" },
                ThreadSafety = "Unsafe",
                Doc = "The world position of a voxel's centre, from its index on each axis.",
            },
            {
                Name = "HeightAt",
                Parameters = {
                    { Name = "x", Type = "number" },
                    { Name = "z", Type = "number" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "The height of the top of the ground here, in metres, or nil where there is none.\n\n"
                    .. "**The top, over caves as well**: the highest surface in the column, which is where "
                    .. "a tree is planted or a player lands from above. Cast a ray with `Workspace:Raycast` "
                    .. "when what you want is the first surface along a direction.",
            },
            {
                Name = "WriteHeights",
                Parameters = {
                    { Name = "corner", Type = "vector" },
                    { Name = "columns", Type = "number" },
                    { Name = "heights", Type = "{ number }" },
                    { Name = "material", Type = "number?" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "Writes a heightmap in one call: one height per column of voxels, `columns` to a row, "
                    .. "row after row along +Z, starting at the column `corner` falls in (its X and Z; Y is "
                    .. "not read). Returns how many voxels it changed.\n\n"
                    .. "**This is the verb for ground that comes from somewhere** -- a generator's noise, an "
                    .. "image, another tool -- where a brush per column would be a quarter of a million "
                    .. "calls. Heights are in world metres and clamped between `MinHeight` and `MaxHeight`; "
                    .. "`material` (default 1) is what new ground is made of. Each column's top moves to its "
                    .. "height -- ground added from the old top up, or taken from it down -- and a cave under "
                    .. "the top stays. A column with no ground at all is laid as a slab 32 metres deep under "
                    .. "the table's lowest height. A height that is not a number is skipped.",
            },
            {
                Name = "Clear",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Removes every voxel. The terrain is empty afterwards and `VoxelSize` becomes "
                    .. "settable again.",
            },
            {
                Name = "Compact",
                Parameters = {},
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "Does nothing and returns 0.\n\n"
                    .. "It converted the old terrain's voxel columns back to heights. Every edit now leaves "
                    .. "the voxels as compact as they can be, so there is nothing left for it to do. It stays "
                    .. "so scripts that call it keep running.",
            },
        } :: { Schema.Method },
    },
    {
        Name = "WeldConstraint",
        Extends = "Instance",
        Module = "scene",
        Parents = InTheWorld,
        Storage = "WeldConstraint",
        Doc = "The same joint, with the offset CAPTURED rather than authored. When it becomes active it "
            .. "records where the two parts are relative to each other and holds that; a Weld is told the "
            .. "relationship and a WeldConstraint reads it off the world. That difference is the whole reason "
            .. "both names exist: authoring an offset by hand for two parts already in the right place is "
            .. "arithmetic nobody should have to do, and capturing one when you meant to specify it is a "
            .. "joint that silently depends on where things happened to be.",
        Properties = {
            {
                Name = "Part0",
                Type = "BasePart?",
                Doc = "The anchor, as on Weld.",
            },
            {
                Name = "Part1",
                Type = "BasePart?",
                Doc = "The driven part, as on Weld.",
            },
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether the joint holds. Going from false to true captures the relative transform afresh, "
                    .. "which is how a part is re-welded somewhere else: move it, then enable.",
            },
            {
                Name = "Active",
                Type = "boolean",
                ReadOnly = true,
                Doc = "Whether it is currently holding: Enabled, with both parts set and both in the world. The "
                    .. "distinction matters because a constraint with one part missing is not an error -- it is "
                    .. "half-built, which is what every script that assigns the two properties on separate lines "
                    .. "briefly produces.",
            },
        } :: { Schema.Property },
    },
    {
        -- Module `scene` rather than `physics`, and that is layering rather
        -- than taste: `physics` is L2 and `scene` is L3, so the physics module
        -- cannot register a class into the scene registry at all. The class is
        -- storage; the meaning is applied by the glue above both, exactly as
        -- `MeshPart` holds a URN that `render` interprets.
        Name = "CharacterBody",
        Extends = "BasePart",
        Module = "scene",
        Storage = "CharacterBody",
        Doc = "A capsule that walks: the player, or anything that should climb a ramp and step over a kerb "
            .. "instead of tumbling. It is a controller rather than a rigid body -- it sweeps its own shape and "
            .. "moves at the velocity you give it, which is why it does not tip over and why gravity is yours to "
            .. "apply through Move. There is no Humanoid and no HumanoidRootPart: this is one instance (§2.7 "
            .. "divergence 13). "
            .. "Two characters BLOCK each other and neither pushes the other. Walking into somebody who is "
            .. "standing still stops you and leaves them where they were, however fast you were going and "
            .. "however long you keep walking -- shoving, knockback and crowd flow are game rules, and a game "
            .. "writes them by moving the other character itself. What holds the block is the rigid body inside "
            .. "the capsule, so CollisionGroup decides it like any other pair: two characters in a group set "
            .. "never to collide with itself walk straight through one another.",
        Properties = {
            {
                Name = "WalkSpeed",
                Type = "number",
                Default = "16",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How fast Move drives the character along the ground, in metres per second. The direction "
                    .. "passed to Move is scaled by this, so a half-length direction walks at half speed.",
            },
            {
                Name = "JumpSpeed",
                Type = "number",
                Default = "8",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The upward speed Jump imparts, in metres per second. How high that reaches depends on "
                    .. "`Workspace.Gravity`, which is the relationship a game tunes rather than a height.",
            },
            {
                Name = "MaxSlopeAngle",
                Type = "number",
                Default = "46",
                ErrorKey = "scene.err.number_slope_degrees",
                Doc = "The steepest ground, in degrees from horizontal, the character can stand and walk on. "
                    .. "Anything steeper is a wall it slides down rather than a floor it stands on.",
            },
            {
                Name = "AutoStepHeight",
                Type = "number",
                Default = "0.5",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The tallest ledge, in metres, the character walks over rather than into. This is the "
                    .. "single number that separates a character from a capsule: at zero, a kerb stops it.",
            },
            {
                Name = "Grounded",
                Type = "boolean",
                ReadOnly = true,
                Doc = "Whether the character is standing on something walkable as of the last simulation tick. "
                    .. "The same fact `State` reports, as the boolean a jump check actually wants.",
            },
            {
                Name = "State",
                Type = "Enum.CharacterState",
                ReadOnly = true,
                Doc = "Grounded or Airborne. Ground too steep to walk on reads as Airborne, because the question "
                    .. "this answers is whether the character is supported.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "Move",
                Parameters = { { Name = "direction", Type = "vector" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Sets the direction to walk in for the next simulation tick, in world space. Only the "
                    .. "horizontal part is used -- vertical movement is gravity's and Jump's -- and the vector is "
                    .. "scaled by WalkSpeed rather than normalised, so a shorter one walks slower. Call it every "
                    .. "tick while moving; a character told nothing stops.",
            },
            {
                Name = "Jump",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Launches the character upward at JumpSpeed at the next simulation tick, wherever it is. "
                    .. "It does NOT check Grounded, deliberately: `LinearVelocity` is read-only, so a mid-air "
                    .. "check here would make a double jump, a wall jump and a triple jump impossible to write "
                    .. "at all. `if character.Grounded then character:Jump() end` is the old behaviour in one "
                    .. "line, in the game, where a jump policy belongs -- and coyote time and jump buffering "
                    .. "become counters beside it. Calling it every frame is flying: that is the game's bug, "
                    .. "and it is the same in every engine that offers a mechanism rather than a policy. The "
                    .. "TICK stays engine-side: the velocity is applied at the next simulation step and never "
                    .. "inside the call, or a replay diverges.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "Landed",
                Parameters = { { Name = "groundPart", Type = "BasePart?" } },
                Doc = "Fires when the character becomes Grounded after being Airborne, with the part it landed "
                    .. "on -- nil when that is terrain no instance owns. Deferred like every signal here.",
            },
        } :: { Schema.Event },
    },
    {
        Name = "Camera",
        Extends = "PVInstance",
        Module = "render",
        Parents = under("Workspace"),
        Storage = "Camera",
        Doc = "The viewpoint the world is rendered from. A camera is an ordinary instance a script owns and moves; "
            .. "the engine never takes it over, which is why there is no CameraType.",
        Properties = {
            {
                Name = "CFrame",
                Type = "CFrame",
                Doc = "Where the camera is and which way it looks. The look direction is the CFrame's LookVector, "
                    .. "which is -Z.",
            },
            {
                Name = "FieldOfView",
                Type = "number",
                ErrorKey = "scene.err.number_cone_degrees",
                Doc = "The vertical field of view in degrees. Clamped to a sane open interval: zero and 180 both "
                    .. "produce a projection matrix that renders nothing.",
            },
            {
                Name = "NearPlane",
                Type = "number",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "Distance in metres to the near clip plane. Small values buy little and cost depth precision "
                    .. "everywhere, so this is worth raising rather than lowering.",
            },
            {
                Name = "FarPlane",
                Type = "number",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "Distance in metres to the far clip plane; geometry beyond it is not drawn.",
            },
            {
                Name = "Projection",
                Type = "Enum.CameraProjection",
                Doc = "Perspective for a 3D view, Orthographic for a 2D one (the 2D layer, phase 3). An "
                    .. "orthographic camera ignores `FieldOfView` and shows `OrthographicSize` metres above and "
                    .. "below the middle of the view, whatever the distance.",
            },
            {
                Name = "OrthographicSize",
                Type = "number",
                Default = "10",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "Half the height of what an orthographic camera shows, in metres. Its width follows from "
                    .. "the window's shape. Zooming a 2D view is changing this.",
            },
        },
    },
    {
        Name = "PointLight",
        Extends = "Instance",
        Module = "render",
        Parents = OnAPart,
        Storage = "PointLight",
        Doc = "A light radiating equally in every direction. Inside a BasePart or an Attachment it shines from "
            .. "there and moves with it; anywhere else it shines from its own `CFrame` (ADR 0095).",
        Properties = {
            {
                Name = "CFrame",
                Type = "CFrame",
                Doc = "Where the light is. **In the world when nothing holds it** -- a light dropped straight into "
                    .. "the Workspace shines from here, and turns with it -- and relative to the BasePart or "
                    .. "Attachment it sits in otherwise, which the identity leaves exactly where that is. The owner's "
                    .. "call (ADR 0095): a light can stand on its own.",
            },
            {
                Name = "Color",
                Type = "Color3",
                Doc = "The light's colour, multiplied by Brightness. Not clamped, so a channel above one is a legal "
                    .. "over-bright tint.",
            },
            {
                Name = "Brightness",
                Type = "number",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How much light this emits. Zero is off without the instance having to be destroyed.",
            },
            {
                Name = "Range",
                Type = "number",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The radius in metres beyond which this light contributes nothing, which is what lets the "
                    .. "renderer skip it for distant geometry.",
            },
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this light contributes anything. Off is not the same as a Brightness of zero: a "
                    .. "disabled light is skipped before the renderer counts it against the light budget, so "
                    .. "turning a room's lights off gives the rest of the scene the slots back.",
            },
            {
                Name = "Shadows",
                Type = "boolean",
                Doc = "Whether this light casts shadows.\n\n**It costs SIX tiles of a sixteen-tile atlas**, because "
                    .. "a point light shines in every direction and a shadow map does not: it is six maps, one per "
                    .. "face of a cube around the light, where a SpotLight is one. Two point lights and four spots "
                    .. "fill the atlas; three point lights do not fit.\n\nWhen more lights ask than there are tiles, "
                    .. "the ones nearest and largest on screen are served and the rest simply do not cast. A light "
                    .. "that loses is not dimmed and does not flicker: the order is by apparent size with ties "
                    .. "broken by creation order, so it is the same answer on every frame and on every machine.",
            },
        },
    },
    {
        Name = "Decal",
        Extends = "Instance",
        Module = "render",
        Parents = under("BasePart"),
        Storage = "Decal",
        Doc = "An image projected onto whatever lies inside a box (F2): a scorch mark, a footprint, a poster, "
            .. "a crack. **No face to choose and no parent required** -- `CFrame.lookAt(hit.Position, "
            .. "hit.Position + hit.Normal)` from a `Raycast` and a size is the whole placement, and it lands on "
            .. "parts, terrain and blocks alike. Parented to a `BasePart` its `CFrame` is relative to the part, "
            .. "so it moves with it.\n\nA decal MULTIPLIES what it lands on, after lighting, which keeps the "
            .. "surface's own light and shadow on it exactly: it darkens and tints, and it cannot make a surface "
            .. "brighter than it was. A white pixel is no change.",
        Properties = {
            {
                Name = "CFrame",
                Type = "CFrame",
                Doc = "Where the box is -- relative to the parent part when there is one. The image lies in the "
                    .. "box's right and up, and is projected along its look direction.",
            },
            {
                Name = "Size",
                Type = "vector",
                Default = "vector.create(2, 2, 1)",
                Doc = "Width, height and depth of the box, in metres. Only what is inside it is painted, so a "
                    .. "shallow box paints one wall and a deep one reaches round a corner.",
            },
            {
                Name = "Texture",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "The image. Its alpha is how much of it lands; none paints `Color` alone.",
            },
            {
                Name = "Color",
                Type = "Color3",
                Default = "Color3.new(1, 1, 1)",
                Doc = "Multiplies the image.",
            },
            {
                Name = "Transparency",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "0 paints the image as it is; 1 paints nothing.",
            },
        },
    },
    {
        Name = "ParticleEmitter",
        Extends = "Instance",
        Module = "render",
        Parents = OnAPart,
        Storage = "ParticleEmitter",
        Doc = "Sparks, smoke, dust and magic (F2). Parent it to a `BasePart` or an `Attachment`: particles are "
            .. "born at its position and fly along its up direction. **They are a picture, not the world** -- "
            .. "they collide with nothing, a script cannot find one, and they are simulated on the frame rather "
            .. "than the tick -- which is what lets a scene hold thousands for the price of one draw.",
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether it emits continuously. `Emit` works either way: a burst is a separate thing from a stream.",
            },
            {
                Name = "Rate",
                Type = "number",
                Default = "20",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "Particles a second while enabled.",
            },
            {
                Name = "Lifetime",
                Type = "number",
                Default = "2",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "Seconds each particle lives before it is gone.",
            },
            {
                Name = "Speed",
                Type = "number",
                Default = "4",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "Metres a second at birth, along the emitter's up direction.",
            },
            {
                Name = "SpreadAngle",
                Type = "number",
                Default = "15",
                ErrorKey = "scene.err.number_spread_degrees",
                Doc = "How far, in degrees, a particle's direction may stray from straight up: 0 is a jet, 180 a sphere.",
            },
            {
                Name = "Acceleration",
                Type = "vector",
                Default = "vector.create(0, 0, 0)",
                Doc = "Metres a second squared in world space, for the whole of each particle's life. `vector.create(0, -9.81, 0)` is gravity; a small upward one is heat.",
            },
            {
                Name = "Drag",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The fraction of its velocity a particle loses each second: smoke slows, sparks do not.",
            },
            {
                Name = "Color",
                Type = "Color3",
                Default = "Color3.new(1, 1, 1)",
                Doc = "The colour at birth.",
            },
            {
                Name = "ColorEnd",
                Type = "Color3",
                Default = "Color3.new(1, 1, 1)",
                Doc = "The colour at death; life interpolates between the two.",
            },
            {
                Name = "Size",
                Type = "number",
                Default = "0.5",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The width in metres at birth.",
            },
            {
                Name = "SizeEnd",
                Type = "number",
                Default = "0.5",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The width in metres at death.",
            },
            {
                Name = "Transparency",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "0 opaque to 1 invisible, at birth.",
            },
            {
                Name = "TransparencyEnd",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "At death. The default fades each particle out as it dies.",
            },
            {
                Name = "LightEmission",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "0 blends a particle over what is behind it, 1 ADDS its light to it -- fire, sparks, magic -- and between is both. Additive particles never darken anything, which is the point.",
            },
            {
                Name = "Brightness",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "A multiplier on the colour, unclamped: above one is what makes a spark bloom.",
            },
            {
                Name = "Shape",
                Type = "Enum.ParticleShape",
                Default = "Enum.ParticleShape.Soft",
                Doc = "What one particle looks like.",
            },
        },
        Methods = {
            {
                Name = "Emit",
                Parameters = { { Name = "count", Type = "number" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Emits `count` particles at once, whether or not the emitter is enabled: an explosion is a "
                    .. "burst, not a stream. At most 1,000 a call.",
            },
        } :: { Schema.Method },
    },
    {
        Name = "SpotLight",
        Extends = "Instance",
        Module = "render",
        Parents = OnAPart,
        Storage = "SpotLight",
        Doc = "A light confined to a cone about its forward direction -- its holder's, turned by its own "
            .. "`CFrame`, or its own alone when nothing holds it (ADR 0095). A sibling of PointLight rather than "
            .. "a subclass of it: the two share their placement and a few properties, and no behaviour.",
        Properties = {
            {
                Name = "CFrame",
                Type = "CFrame",
                Doc = "Where the light is. **In the world when nothing holds it** -- a light dropped straight into "
                    .. "the Workspace shines from here, and turns with it -- and relative to the BasePart or "
                    .. "Attachment it sits in otherwise, which the identity leaves exactly where that is. The owner's "
                    .. "call (ADR 0095): a light can stand on its own.",
            },
            {
                Name = "Color",
                Type = "Color3",
                Doc = "The light's colour, multiplied by Brightness.",
            },
            {
                Name = "Brightness",
                Type = "number",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How much light this emits.",
            },
            {
                Name = "Range",
                Type = "number",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The distance in metres beyond which this light contributes nothing.",
            },
            {
                Name = "Angle",
                Type = "number",
                ErrorKey = "scene.err.number_cone_degrees",
                Doc = "The full width of the cone in degrees.",
            },
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this light contributes anything. Off is not the same as a Brightness of zero: a "
                    .. "disabled light is skipped before the renderer counts it against the light budget, so "
                    .. "turning a room's lights off gives the rest of the scene the slots back.",
            },
            {
                Name = "Shadows",
                Type = "boolean",
                Doc = "Whether this light casts shadows.\n\n**It costs one tile of a sixteen-tile atlas**, where a "
                    .. "PointLight costs six, so the two are budgeted against each other rather than counted "
                    .. "separately.\n\nWhen more lights ask than there are tiles, the ones nearest and largest on "
                    .. "screen are served and the rest simply do not cast. A light that loses is not dimmed and does "
                    .. "not flicker: the order is by apparent size with ties broken by creation order, so it is the "
                    .. "same answer on every frame and on every machine.",
            },
        },
    },
    {
        Name = "PostEffect",
        Extends = "Instance",
        Module = "render",
        Parents = under("Lighting", "Camera"),
        Tags = { "Abstract" } :: { Schema.ClassTag },
        -- Every effect class stores its own numbers beside this one's `Enabled`:
        -- `World::create` calls every hook down the ancestry, so the base's
        -- component and the class's are two pools, never one attached twice.
        Storage = "PostEffect",
        Doc = "The abstract base of the effects that change the finished picture rather than the world in it "
            .. "(ADR 0096). **Where one is decides whose it is**: directly under `Lighting` it belongs to the "
            .. "world -- saved with the scene and seen by everybody -- and directly under "
            .. "`Workspace.CurrentCamera` it belongs to whoever looks through that camera. Anywhere else it does "
            .. "nothing, and the editor says so on the instance.",
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this effect takes part. Off is not the same as deleting it: a `BloomEffect` that "
                    .. "is off still governs bloom, and governs it to nothing.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "BloomEffect",
        Extends = "PostEffect",
        Module = "render",
        Storage = "BloomEffect",
        Doc = "The glow that bright light spills onto what is around it. **The engine blooms without one**, "
            .. "exactly as this class's defaults do; adding one takes the glow over, and one with `Enabled` "
            .. "off turns it off -- what a 2D game whose white sprites should not shine wants. When several "
            .. "apply, the first enabled one in order wins: `Lighting`'s children first, then the camera's.",
        Properties = {
            {
                Name = "Intensity",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How strongly the glow is added, as a multiple of the engine's own: 1 is the look every "
                    .. "world has without this instance, 2 is twice the glow, 0 is none.",
            },
            {
                Name = "Size",
                Type = "number",
                Default = "24",
                ErrorKey = "scene.err.number_bloom_size",
                Doc = "How far the glow reaches, from 0 to 56. 24 is the engine's own reach; 48 spreads the "
                    .. "same light about twice as far, and fainter for it.",
            },
            {
                Name = "Threshold",
                Type = "number",
                Default = "1.1",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How bright a pixel must be, after exposure, before it glows. Exposure maps a frame's "
                    .. "average to about 0.45, so the default blooms what is a little over twice as bright as "
                    .. "the average; 0 makes everything glow a little.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "ColorCorrectionEffect",
        Extends = "PostEffect",
        Module = "render",
        Storage = "ColorCorrectionEffect",
        Doc = "A grade over the whole picture: brighter or darker, flatter or punchier, greyer or more vivid, "
            .. "and tinted. It works on the exposed light, before the tone curve compresses it, so a "
            .. "highlight it brightens still rolls off rather than clipping. **Every enabled one applies**, "
            .. "one after another in order -- `Lighting`'s children first, then the camera's -- and each "
            .. "applies its tint, then its saturation, then its contrast, then its brightness.",
        Properties = {
            {
                Name = "Brightness",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_minus_one_to_one",
                Doc = "Light added to every pixel, from -1 to 1. A frame's average sits near 0.45 after "
                    .. "exposure, so 0.1 is a gentle lift and -0.2 a clear darkening; the darkest pixels "
                    .. "stop at black.",
            },
            {
                Name = "Contrast",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_minus_one_to_one",
                Doc = "How far every pixel is pushed from the frame's average brightness, from -1 to 1. 0 "
                    .. "leaves it, 0.5 spreads it half as far again, and -1 flattens the picture to one grey.",
            },
            {
                Name = "Saturation",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_minus_one_to_one",
                Doc = "How vivid colours are, from -1 to 1. -1 is black and white, 0 leaves them, 1 is twice "
                    .. "as far from grey.",
            },
            {
                Name = "TintColor",
                Type = "Color3",
                Default = "Color3.new(1, 1, 1)",
                Doc = "Multiplies the picture. White changes nothing; a warm white warms it.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "BlurEffect",
        Extends = "PostEffect",
        Module = "render",
        Storage = "BlurEffect",
        Doc = "Softens the whole world picture, as behind a pause menu. The interface is drawn after it and "
            .. "stays sharp. **Several combine as blurs do in optics**: the sizes add by their squares, so two "
            .. "blurs of 8 are one of about 11.3, not one of 16.",
        Properties = {
            {
                Name = "Size",
                Type = "number",
                Default = "24",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How far a pixel's light is spread, in pixels of a picture 1,080 lines tall -- most of it "
                    .. "lands within this distance. It scales with the window, so a blur looks the same at any "
                    .. "size. 0 is no blur, and costs nothing.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "DepthOfFieldEffect",
        Extends = "PostEffect",
        Module = "render",
        Storage = "DepthOfFieldEffect",
        Doc = "Focus by distance, as a camera lens has it: a band of the world in focus, and what is nearer or "
            .. "further softening away from it. The sky counts as infinitely far. When several apply, the "
            .. "first enabled one wins, in the order `Lighting`'s children and then the camera's. A machine "
            .. "whose graphics settings turn depth of field off draws without it.",
        Properties = {
            {
                Name = "FocusDistance",
                Type = "number",
                Default = "25",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The distance from the camera, in metres, that is sharpest.",
            },
            {
                Name = "InFocusRadius",
                Type = "number",
                Default = "10",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How many metres either side of `FocusDistance` stay fully sharp before the softening "
                    .. "begins.",
            },
            {
                Name = "NearIntensity",
                Type = "number",
                Default = "0.5",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How soft what is nearer than the sharp band becomes, from 0 (not at all) to 1.",
            },
            {
                Name = "FarIntensity",
                Type = "number",
                Default = "0.5",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How soft what is further than the sharp band becomes, from 0 (not at all) to 1.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "SunRaysEffect",
        Extends = "PostEffect",
        Module = "render",
        Storage = "SunRaysEffect",
        Doc = "Shafts of light streaming from the sun past whatever stands in front of it. They come from the "
            .. "open sky around the sun, so a tree or a wall cuts them, and they fade away as the sun leaves "
            .. "the view or sets. When several apply, the first enabled one wins, in the order `Lighting`'s "
            .. "children and then the camera's. A machine whose graphics settings turn sun rays off draws "
            .. "without them.",
        Properties = {
            {
                Name = "Intensity",
                Type = "number",
                Default = "0.25",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How bright the shafts are, from 0 to 1.",
            },
            {
                Name = "Spread",
                Type = "number",
                Default = "0.5",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How far the shafts reach from the sun, from 0 (a halo) to 1 (across the screen).",
            },
        } :: { Schema.Property },
    },
    {
        Name = "Atmosphere",
        Extends = "Instance",
        Module = "render",
        Parents = under("Lighting"),
        Storage = "Atmosphere",
        Doc = "The air between the camera and everything it sees. Distance fades into it and it thins with "
            .. "height, it is lit by the sun and glows around it, and it tints the sky towards the horizon so "
            .. "the ground and the sky meet. **It counts only directly under `Lighting`**, and only the first "
            .. "one there. With one, `Lighting.FogStart`, `FogEnd` and `FogColor` are kept but not used.",
        Properties = {
            {
                Name = "Density",
                Type = "number",
                Default = "0.35",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How thick the air is, from 0 to 1. 0 is perfectly clear; at the default, half of what "
                    .. "stands 280 metres away still shows through at the height of `Offset`; at 1, half is "
                    .. "gone within 35 metres.",
            },
            {
                Name = "Offset",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_finite",
                Doc = "The height, in metres, at which the air has exactly its `Density`. It is thicker below "
                    .. "and thinner above, by `Decay`, so raising it lifts the haze up a valley's sides.",
            },
            {
                Name = "Color",
                Type = "Color3",
                Default = "Color3.fromRGB(196, 210, 230)",
                Doc = "The colour of the air, lit. Distant things fade towards it, and the sky turns towards it "
                    .. "at the horizon. The sun's height still decides how bright it is: the same colour is dark "
                    .. "at night.",
            },
            {
                Name = "Decay",
                Type = "number",
                Default = "0.1",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How quickly the air thins with height, from 0 to 1. 0 keeps it equally thick at every "
                    .. "altitude; at the default it halves every 100 metres of height; at 1, every 10.",
            },
            {
                Name = "Glare",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_zero_to_ten",
                Doc = "The bright lobe the air makes around the sun, from 0 to 10. It brightens the sky near "
                    .. "the sun and the distance seen towards it.",
            },
            {
                Name = "Haze",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_zero_to_ten",
                Doc = "Extra thickness near the horizon, from 0 to 10: it washes the sky's horizon band into "
                    .. "the air's colour and buries far distance deeper than `Density` alone.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "Sky",
        Extends = "Instance",
        Module = "render",
        Parents = under("Lighting"),
        Storage = "Sky",
        Doc = "What the sky shows: six images around the world, the sun's and the moon's look, stars and "
            .. "clouds. **It counts only directly under `Lighting`**, and only the first one there. The images "
            .. "are also what surfaces reflect and are lit by. **The sun stays on the clock**: its direction "
            .. "comes from `Lighting.ClockTime` and `GeographicLatitude` whatever the images show, so "
            .. "shadows and the drawn sun agree -- choose images without a sun painted in, or turn "
            .. "`CelestialBodiesShown` off.",
        Properties = {
            {
                Name = "SkyboxBack",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "The image towards +Z, before `SkyboxOrientation` turns the set. None of the six means "
                    .. "the engine's own sky, drawn from the time of day.",
            },
            {
                Name = "SkyboxDown",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "The image straight down.",
            },
            {
                Name = "SkyboxFront",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "The image towards -Z, which a camera with no rotation looks at.",
            },
            {
                Name = "SkyboxLeft",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "The image towards -X.",
            },
            {
                Name = "SkyboxRight",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "The image towards +X.",
            },
            {
                Name = "SkyboxUp",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "The image straight up.",
            },
            {
                Name = "SkyboxOrientation",
                Type = "vector",
                Default = "vector.create(0, 0, 0)",
                Doc = "Turns the six images together, in degrees about X, Y and Z -- a mountain range painted "
                    .. "into the images is moved to where the world wants it.",
            },
            {
                Name = "SunTexture",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "An image drawn as the sun's disc, lit by the sun's colour. None draws a round, soft-edged "
                    .. "disc.",
            },
            {
                Name = "MoonTexture",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "An image drawn as the moon's disc at night. None draws a plain pale disc.",
            },
            {
                Name = "SunAngularSize",
                Type = "number",
                Default = "2.3",
                ErrorKey = "scene.err.number_angular_size",
                Doc = "How wide the sun looks, in degrees across, above 0 and at most 60. The real sun is about "
                    .. "half a degree; the default is larger so a sunset reads in a short day.",
            },
            {
                Name = "MoonAngularSize",
                Type = "number",
                Default = "2",
                ErrorKey = "scene.err.number_angular_size",
                Doc = "How wide the moon looks, in degrees across, above 0 and at most 60.",
            },
            {
                Name = "StarCount",
                Type = "number",
                Default = "3000",
                ErrorKey = "scene.err.number_star_count",
                Doc = "How many stars cover the whole sky at night, from 0 to 10,000. They come out as the "
                    .. "sun goes down and are the same stars in the same places every night.",
            },
            {
                Name = "CelestialBodiesShown",
                Type = "boolean",
                Default = "true",
                Doc = "Whether the sun, the moon and the stars are drawn. Off leaves only the images and the "
                    .. "clouds -- the light still comes from where the clock puts the sun.",
            },
            {
                Name = "CloudCover",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How much of the sky the clouds cover, from 0 (none) to 1 (overcast). They drift with the "
                    .. "game's own clock, so a paused game's clouds stand still and a replay's move the same way.",
            },
            {
                Name = "CloudDensity",
                Type = "number",
                Default = "0.5",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How thick each cloud is, from 0 (wisps the sky shows through) to 1 (solid, with dark "
                    .. "undersides).",
            },
            {
                Name = "CloudColor",
                Type = "Color3",
                Default = "Color3.new(1, 1, 1)",
                Doc = "The clouds' colour where the sun lights them.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "InputContext",
        Extends = "Instance",
        Module = "input",
        Storage = "InputContext",
        Doc = "A group of `InputAction` children that are live together (§2.4, ADR 0029). A menu opening is one "
            .. "context enabled and another disabled, rather than a pile of conditionals inside every handler. "
            .. "Parent it anywhere -- the convention is under the Script that owns it -- because a context is "
            .. "reached by being enabled, not by where it sits.",
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this context and its actions take part in resolution at all. A disabled context "
                    .. "consumes nothing, so a lower-priority one bound to the same key starts firing the tick "
                    .. "this goes false -- which is exactly what closing a menu should do.",
            },
            {
                Name = "Priority",
                Type = "number",
                Default = "0",
                Doc = "Resolution order, highest first. It orders FALLTHROUGH and nothing else -- in particular "
                    .. "it does not decide which clock the context is dispatched on, which is `Rate` (ADR 0039). "
                    .. "Ties are broken by a stable order the engine reproduces on every run (R10), so two "
                    .. "contexts at the same priority never swap places between one run and the next.",
            },
            {
                Name = "Sink",
                Type = "boolean",
                Default = "false",
                Doc = "Whether this context CONSUMES the inputs its bindings name, hiding them from every "
                    .. "lower-priority context. A modal dialog sinks; a HUD does not. Sinking is per input and "
                    .. "not per context: a sinking context that binds Escape hides Escape and leaves W alone.",
            },
            {
                Name = "Rate",
                Type = "Enum.InputRate",
                Default = "Enum.InputRate.Simulation",
                Doc = "Which clock this context is dispatched on (ADR 0039). `Simulation` by default, because "
                    .. "that is the safe one: an action nobody thought about fires where determinism holds and "
                    .. "where the "
                    .. "input replay can see it. `Render` is for camera look and for UI, and an action read from "
                    .. "a Render context is frame-rate-dependent by construction.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "InputAction",
        Extends = "Instance",
        Module = "input",
        Parents = under("InputContext"),
        Storage = "InputAction",
        Doc = "A named thing the player can do, decoupled from the input that does it (§2.4, ADR 0029). Game "
            .. "code connects to the action; `InputBinding` children say what drives it. That indirection is "
            .. "the whole point: rebinding is a property write, and a prompt can ask the action what key to "
            .. "draw.\n\nParent it to an `InputContext`. An action outside one is inert -- resolution walks "
            .. "contexts, so an action nothing groups is an action nothing dispatches.",
        Properties = {
            {
                Name = "Type",
                Type = "Enum.InputActionType",
                Default = "Enum.InputActionType.Bool",
                Doc = "What this action produces, and therefore what `GetState` returns. It belongs to the "
                    .. "action rather than to the binding, because one action may be driven by a key, a stick "
                    .. "and an on-screen button at once and all three have to answer in the same currency.",
            },
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this action resolves. A disabled action consumes nothing even inside a sinking "
                    .. "context, so disabling one hands its input back to whatever is underneath.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "GetState",
                Parameters = {},
                Returns = { "boolean | number | Vector2 | vector" },
                Doc = "This action's value as of the current tick -- a boolean for `Bool`, a number for "
                    .. "`Direction1D`, a `Vector2` for `Direction2D` and `ViewportPosition`, and a `vector` for "
                    .. "`Direction3D`. It is a SNAPSHOT: two calls inside one tick agree, and a recorded input "
                    .. "stream hands the same answers back with no hardware attached, which is what makes the "
                    .. "replay a replay of input rather than of a bot.",
            },
            {
                Name = "GetPreferredBinding",
                Parameters = { { Name = "deviceType", Type = "Enum.InputDeviceType?", Default = "nil" } },
                Returns = { "InputBinding?" },
                Doc = 'The binding a prompt should draw for this action -- "Press [E]". With a device type, '
                    .. "the first child binding belonging to that family in document order; without one, the "
                    .. "family of `InputService.LastInputDeviceType`, so a prompt follows whatever the player "
                    .. "last touched. nil when the action has no binding for that device.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "Pressed",
                Parameters = {},
                Doc = "Fired on the tick a `Bool` action becomes true. Never fires for any other action type: "
                    .. "an axis has no moment of being pressed.",
            },
            {
                Name = "Released",
                Parameters = {},
                Doc = "Fired on the tick a `Bool` action becomes false, including when the action or its "
                    .. "context is disabled while it was held -- a held key that stops being delivered has been "
                    .. "released as far as the game is concerned, and leaving it stuck true is how a character "
                    .. "keeps running after a menu opens.",
            },
            {
                Name = "StateChanged",
                Parameters = {},
                Doc = "Fired on any tick this action's value changed, whatever its type. It carries NO "
                    .. "arguments and the handler reads `GetState()` -- the same arrangement `Destroying` and "
                    .. "`GetPropertyChangedSignal` use, and for the same reason (ADR 0039): the queue between "
                    .. "the engine and the VM carries POD facts, an action's value is one of four types, and a "
                    .. "signal whose argument had to be rebuilt at drain time would be a fire that captured "
                    .. "nothing. Input dispatches once per tick, so the value read in the handler is the value "
                    .. "that caused the fire.",
            },
        } :: { Schema.Event },
    },
    {
        Name = "InputBinding",
        Extends = "Instance",
        Module = "input",
        Parents = under("InputAction"),
        Storage = "InputBinding",
        Doc = "One physical input that drives its parent `InputAction` (§2.4). An action may have several, and "
            .. "they are ORed: a key, a gamepad stick and a UI button on one action are three ways to do one "
            .. "thing.\n\nWhich fields matter depends on the parent's `Type`. A `Bool` action reads `KeyCode`; "
            .. "a `Direction2D` action reads either `KeyCode` naming a 2D source such as `LeftThumbstick`, or "
            .. "the four composite keys. A binding whose fields do not suit its parent is inert rather than an "
            .. "error -- an action's type is a property somebody can change after the binding was built.",
        Properties = {
            {
                Name = "KeyCode",
                Type = "Enum.KeyCode",
                Default = "Enum.KeyCode.Unknown",
                Doc = "The input this binding names. `Unknown` means unbound, which is why the enum carries "
                    .. "that item: an unset binding is expressible without the property being nullable, and the "
                    .. "engine's value domain stays a closed set (ADR 0039).",
            },
            {
                Name = "Up",
                Type = "Enum.KeyCode",
                Default = "Enum.KeyCode.Unknown",
                Doc = "The composite key for +Y, on a `Direction1D` or `Direction2D` action. The four "
                    .. "composites are how a keyboard drives an axis: W/A/S/D is one binding, not four.",
            },
            {
                Name = "Down",
                Type = "Enum.KeyCode",
                Default = "Enum.KeyCode.Unknown",
                Doc = "The composite key for -Y.",
            },
            {
                Name = "Left",
                Type = "Enum.KeyCode",
                Default = "Enum.KeyCode.Unknown",
                Doc = "The composite key for -X.",
            },
            {
                Name = "Right",
                Type = "Enum.KeyCode",
                Default = "Enum.KeyCode.Unknown",
                Doc = "The composite key for +X.",
            },
            {
                Name = "Scale",
                Type = "number",
                Default = "1",
                Doc = "What this binding's value is multiplied by before it reaches the action. Mouse look "
                    .. "wants a small one and a stick wants 1; a negative one inverts an axis, which is what a "
                    .. 'settings screen\'s "invert Y" writes.',
            },
            {
                Name = "DisplayName",
                Type = "string",
                Default = '""',
                Doc = "What a prompt shows for this binding. A localization key is allowed and is the intended "
                    .. 'use (§6): a binding that says "ui.prompt.interact" translates and one that says '
                    .. '"Interact" does not. Empty means the engine has nothing to offer and the prompt '
                    .. "should fall back to the key's own name.\n\n**The engine never acts on this and never "
                    .. "will**, which is what makes it different from an `Inert` property: it is data a GAME "
                    .. "reads back through `GetPreferredBinding` to draw its own prompt. There is no prompt "
                    .. "widget in v1 and there is not meant to be one -- a rebinding screen is a game's "
                    .. "screen.",
            },
            {
                Name = "Image",
                Type = "Content",
                ContentKind = "Texture",
                Default = '""',
                Doc = "A glyph for this binding, as an `asset://` URI. Empty means none -- a prompt with no "
                    .. "glyph falls back to `DisplayName`, which is why the two are set together or not at "
                    .. "all.\n\n**The engine never acts on this and never will**, for exactly the reason "
                    .. "`DisplayName` gives: it is the same datum in a picture instead of a word, read back "
                    .. "by a GAME through `GetPreferredBinding` to draw its own prompt. Nothing in the engine "
                    .. "draws a prompt, so nothing in the engine loads this texture -- an `ImageLabel` the "
                    .. "game points at it does.",
            },
            {
                Name = "DeviceType",
                Type = "Enum.InputDeviceType",
                ReadOnly = true,
                Doc = "Which device family this binding belongs to, DERIVED from `KeyCode` (ADR 0039). It is "
                    .. "read-only because a settable one could disagree with the key beside it -- "
                    .. "`DeviceType = Gamepad` on a binding whose KeyCode is W is a statement the engine would "
                    .. "then have to choose whether to believe. Anything that is not a gamepad input reports "
                    .. "`KeyboardMouse`, `Unknown` included.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "ScreenGui",
        Extends = "Instance",
        Module = "ui",
        Parents = under("UIService"),
        Storage = "ScreenGui",
        Doc = "The root of one screen-space UI tree, parented to `UIService` (§2.2). Everything under it is "
            .. "laid out against the window and drawn over the world.\n\nIt is the unit of layout: a write "
            .. "that changes a layout marks the nearest ScreenGui dirty, and a screen nothing changed does not "
            .. "run the solver at all. That is a design constraint rather than an optimisation -- the "
            .. 'benchmark asserts ZERO solver invocations on an idle frame, because "about zero '
            .. 'microseconds" is a measurement of the clock.',
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this tree is laid out and drawn at all. A disabled ScreenGui costs one boolean "
                    .. "read per frame; it does not cost a layout.",
            },
            {
                Name = "DisplayOrder",
                Type = "number",
                Default = "0",
                Doc = "Which ScreenGui draws on top when two overlap, highest last. `ZIndex` orders WITHIN a "
                    .. "tree and this orders BETWEEN trees, so a HUD and a modal never have to agree on a "
                    .. "shared numbering.",
            },
            {
                Name = "ScreenInsets",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this tree lays out inside `UIService.SafeAreaInsets` rather than against the "
                    .. "whole window. On by default: a full-bleed background is the exception and a HUD "
                    .. "clipped by a notch is the failure.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "BillboardGui",
        Extends = "Instance",
        Module = "ui",
        Parents = OnAPart,
        Storage = "BillboardGui",
        Doc = "A UI tree hung in the world and turned to face the camera (F3): a name over a head, a health "
            .. "bar over a crate, a marker on an objective. Its children are laid out exactly as a "
            .. "`ScreenGui`'s are, against a canvas of `Size`, and drawn IN the world -- behind what is in "
            .. "front of it, in the same light as the picture around it -- rather than over it.\n\n"
            .. "It hangs over `Adornee`, or over its parent when that is a part and `Adornee` is empty. "
            .. "Its buttons are pressed like the screen's: the pointer's ray finds them in the world, "
            .. "and something solid in front hides them unless `AlwaysOnTop` is set. A screen element "
            .. "over the same pixel wins.",
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether it is laid out and drawn at all.",
            },
            {
                Name = "Adornee",
                Type = "BasePart?",
                Doc = "The part it hangs over. Empty means its parent, when its parent is a part; a billboard "
                    .. "with neither is not drawn.",
            },
            {
                Name = "Size",
                Type = "UDim2",
                Default = "UDim2.new(0, 200, 0, 50)",
                Doc = "How big it is. **The scale is in metres and the offset in pixels**, and the two mean "
                    .. "different things: a scale keeps its size in the world, so it shrinks with distance "
                    .. "like anything else there; an offset keeps its size on the screen, so a name tag "
                    .. "is as readable across the map as up close. The children lay out against the pixel "
                    .. "size either way.",
            },
            {
                Name = "WorldOffset",
                Type = "vector",
                Default = "vector.zero",
                Doc = "Where its centre sits relative to the adornee's centre, in metres and in the world's "
                    .. "own axes -- `vector.create(0, 3, 0)` is three metres above it however the part is "
                    .. "turned.",
            },
            {
                Name = "AlwaysOnTop",
                Type = "boolean",
                Default = "false",
                Doc = "Whether it shows through walls. Off, it is hidden by whatever is in front of it, like "
                    .. "anything in the world.",
            },
            {
                Name = "MaxDistance",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How far from the camera it stops being drawn, in metres. Zero means no limit.",
            },
            {
                Name = "Brightness",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How bright its colours are in the world's light, which is brighter than the screen's: "
                    .. "above one, it glows and blooms.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "SurfaceGui",
        Extends = "Instance",
        Module = "ui",
        Parents = under("BasePart"),
        Storage = "SurfaceGui",
        Doc = "A UI tree drawn onto one face of a part (F3): a screen on a wall, a sign, a scoreboard, a "
            .. "label on a crate. Its children are laid out exactly as a `ScreenGui`'s are, against a "
            .. "canvas the size of the face at `PixelsPerMetre`, and it moves, turns and is hidden with the "
            .. "part.\n\n"
            .. "It covers `Adornee`, or its parent when that is a part and `Adornee` is empty. Its "
            .. "buttons are pressed like the screen's, from in front of the face: the pointer's ray finds "
            .. "them in the world, and something solid in front hides them unless `AlwaysOnTop` is set.",
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether it is laid out and drawn at all.",
            },
            {
                Name = "Adornee",
                Type = "BasePart?",
                Doc = "The part it covers. Empty means its parent, when its parent is a part.",
            },
            {
                Name = "Face",
                Type = "Enum.Face",
                Default = "Enum.Face.Front",
                Doc = "Which face of the part it covers. `Front` is the one the part's `LookVector` comes out "
                    .. "of, and every side face is read the right way up.",
            },
            {
                Name = "PixelsPerMetre",
                Type = "number",
                Default = "50",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How many of the canvas's pixels fit in a metre of the face, which is what a child's "
                    .. "offset and a `TextSize` are measured in. Fifty makes 14-point text about a hand high "
                    .. "on a wall; more is finer text on the same face.",
            },
            {
                Name = "AlwaysOnTop",
                Type = "boolean",
                Default = "false",
                Doc = "Whether it shows through what is in front of the face.",
            },
            {
                Name = "Brightness",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How bright its colours are in the world's light: above one, a screen that glows.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "UIObject",
        Extends = "Instance",
        Module = "ui",
        Parents = InAUi,
        Tags = { "Abstract", "NotCreatable" } :: { Schema.ClassTag },
        Storage = "UIObject",
        Doc = "Anything that occupies a rectangle on screen (§2.2). Every property here is one the layout "
            .. "solver or the 2D pass reads, which is what makes it the base rather than a convenience.\n\n"
            .. "Coordinates are `UDim2`: a fraction of the parent plus a pixel offset, on each axis. That "
            .. "pair is what lets one layout be correct at every resolution, and it is why the reference "
            .. "images are recorded at two of them.",
        Properties = {
            {
                Name = "Position",
                Type = "UDim2",
                Default = "UDim2.new()",
                Doc = "Where this object's `AnchorPoint` sits inside its parent.",
            },
            {
                Name = "Size",
                Type = "UDim2",
                Default = "UDim2.new()",
                Doc = "How big it is, relative to its parent plus an offset. Overridden on an axis that "
                    .. "`AutomaticSize` covers.",
            },
            {
                Name = "AnchorPoint",
                Type = "Vector2",
                Default = "Vector2.zero",
                Doc = "Which point of THIS object `Position` places, in fractions of its own size. (0,0) is "
                    .. "its top-left corner and (0.5,0.5) its middle -- which is what makes centring "
                    .. "something one property rather than an arithmetic apology.",
            },
            {
                Name = "Rotation",
                Type = "number",
                Default = "0",
                Doc = "Degrees clockwise about the anchor point. It rotates the DRAWING and not the layout: a "
                    .. "rotated element still occupies its unrotated rectangle, which is what stops one "
                    .. "spinning icon reflowing a list every frame.\n\n**Descendants turn with it**, and a "
                    .. "descendant's own `Rotation` composes on top -- a dial rotated 30 degrees with a "
                    .. "needle rotated 90 draws the needle at 120, about the dial's anchor and then its "
                    .. "own.\n\nThree things the turn does not reach, all for the reason `UICorner` gives: "
                    .. "layout, which never sees it; the hit test, which stays the unrotated rectangle, "
                    .. "because a button you could see in one place and click in another is worse than one "
                    .. "that does not turn; and `ClipsDescendants`, whose rectangle stays axis-aligned, so a "
                    .. "turned child of a clipping parent is clipped by the parent's upright box.",
            },
            {
                Name = "BackgroundColor",
                Type = "Color3",
                Default = "Color3.new(1, 1, 1)",
                Doc = "The fill behind the object's own content. No `3` suffix (§2.5, divergence #11).",
            },
            {
                Name = "BackgroundTransparency",
                Type = "number",
                Default = "0",
                Doc = "0 opaque, 1 invisible. A fully transparent background still lays out and still hit-tests "
                    .. "-- `Visible` is the property that stops both.",
            },
            {
                Name = "Visible",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this object and its descendants are laid out, drawn and hit-tested. All three "
                    .. "together: an invisible element that still swallowed clicks is the defect this "
                    .. "sentence exists to rule out.",
            },
            {
                Name = "ZIndex",
                Type = "number",
                Default = "0",
                Doc = "Draw order within this ScreenGui, highest last, ties broken by document order. It does "
                    .. "not affect layout and does not nest: one flat ordering across the tree, because a "
                    .. "per-parent stacking context is the part of CSS nobody can hold in their head.",
            },
            {
                Name = "LayoutOrder",
                Type = "number",
                Default = "0",
                Doc = "Where this child sits in a `UIListLayout` whose `SortOrder` is `LayoutOrder`. Ignored "
                    .. "otherwise.",
            },
            {
                Name = "AutomaticSize",
                Type = "Enum.AutomaticSize",
                Default = "Enum.AutomaticSize.None",
                Doc = "Which axes size themselves to fit the contents, overriding that half of `Size`.",
            },
            {
                Name = "ClipsDescendants",
                Type = "boolean",
                Default = "false",
                Doc = "Whether descendants are clipped to this object's rectangle. What a scrolling list needs "
                    .. "and what a drop shadow must not have.",
            },
            {
                Name = "AbsolutePosition",
                Type = "Vector2",
                ReadOnly = true,
                Transient = true,
                Doc = "Where the solver put this object's top-left corner, in window pixels. Read-only "
                    .. "because it is an OUTPUT: writing it would be arguing with the layout rather than "
                    .. "changing it. Valid after the layout that produced it, which is the frame the "
                    .. "property was read in.",
            },
            {
                Name = "AbsoluteSize",
                Type = "Vector2",
                ReadOnly = true,
                Transient = true,
                Doc = "The size the solver gave it, in window pixels.",
            },
        } :: { Schema.Property },
        Events = {
            {
                Name = "Activated",
                Parameters = {},
                Doc = "Fired when a pointer press and release both land on this object. Device-neutral by "
                    .. "design (§2.5, divergence #17): a click, a tap and a bound gamepad button all produce "
                    .. "it, which is why there is no `MouseButton1Click`.",
            },
            {
                Name = "PointerEntered",
                Parameters = {},
                Doc = "Fired when the pointer moves onto this object. Replaces `MouseEnter`, and is named for "
                    .. "a pointer rather than a mouse because a finger is one too.",
            },
            {
                Name = "PointerExited",
                Parameters = {},
                Doc = "Fired when the pointer moves off it.",
            },
        } :: { Schema.Event },
    },
    {
        Name = "Frame",
        Extends = "UIObject",
        Module = "ui",
        Doc = "A rectangle and nothing else: a background, a border of children, and the layout it imposes on "
            .. "them. The element every UI is mostly made of.",
    },
    {
        Name = "TextLabel",
        Extends = "UIObject",
        Module = "ui",
        Storage = "TextLabel",
        Doc = "Text in a box (§2.2). Markup inside one label is `RichText`'s. There is no complex-script "
            .. "shaping and no kerning -- codepoints are laid out left to right by their advances. That is a "
            .. "known gap for Arabic, Devanagari and Thai, and the seam a text shaper would sit behind is "
            .. "deliberately left open.",
        Properties = {
            {
                Name = "Text",
                Type = "string",
                Default = '""',
                Doc = "What it says. UTF-8, and a codepoint the font has no glyph for draws the font's "
                    .. "`.notdef` box rather than nothing: a label that silently drops characters is the "
                    .. "failure mode that ships.",
            },
            {
                Name = "TextColor",
                Type = "Color3",
                Default = "Color3.new(0, 0, 0)",
                Doc = "No `3` suffix (§2.5, divergence #11).",
            },
            {
                Name = "TextTransparency",
                Type = "number",
                Default = "0",
                Doc = "How see-through the text is: 0 solid, 1 not drawn at all. The text's own, apart from "
                    .. "`BackgroundTransparency`, so a label can fade its words and keep its box, or the "
                    .. "other way round. Values outside 0 to 1 are kept as written and drawn as the nearer end.",
            },
            {
                Name = "TextSize",
                Type = "number",
                Default = "14",
                Doc = "The em size in pixels. An atlas is rasterized per font and integer size, so a "
                    .. "fractional one is rounded for rasterization and the layout still uses what was asked "
                    .. "for.",
            },
            {
                Name = "Font",
                Type = "Content",
                ContentKind = "Font",
                Default = '""',
                Doc = "The typeface, as an `asset://` URI to a TrueType file. Empty is the engine's own "
                    .. "default face -- **Inter**, OFL 1.1, vendored and staged beside the binary -- and so is "
                    .. "the literal name `Inter`.\n\nA name that cannot be resolved falls back to the default and "
                    .. "says so ONCE rather than once a frame: a label that vanished because its font name had "
                    .. "a typo is a bug report about the label. A build whose content directory has no font at "
                    .. "all falls back further, to a built-in vector face that is ASCII only, one weight and no "
                    .. "kerning -- text still draws, which is the point of having a fallback.\n\nGlyphs are "
                    .. "rasterised on demand and cached by face, size and codepoint, so a `TextSize` a tween is "
                    .. "animating costs one entry per quarter-pixel rather than one per frame.",
            },
            {
                Name = "TextXAlignment",
                Type = "Enum.HorizontalAlignment",
                Default = "Enum.HorizontalAlignment.Center",
                Doc = "Where the text sits across the box: `Left`, `Center` or `Right`. Named for the text, "
                    .. "because a layout's `HorizontalAlignment` places children and this places words; the "
                    .. "values are the same enum a layout uses.",
            },
            {
                Name = "TextYAlignment",
                Type = "Enum.VerticalAlignment",
                Default = "Enum.VerticalAlignment.Center",
                Doc = "Where the text sits down the box: `Top`, `Center` or `Bottom`.",
            },
            {
                Name = "TextWrapped",
                Type = "boolean",
                Default = "false",
                Doc = "Whether lines break at the box's width. Breaks at spaces, and mid-word only for a word "
                    .. "wider than the box -- a word cut in half at a random letter is worse than one that "
                    .. "overhangs.",
            },
            {
                Name = "RichText",
                Type = "boolean",
                Default = "false",
                Doc = "Whether `Text` is read as markup, so one label can change colour, size and weight "
                    .. "part-way through (F3).\n\n"
                    .. "The tags: `<b>` bold, `<i>` italic, `<u>` underlined, `<s>` struck through, "
                    .. '`<font color="#rrggbb" size="20" transparency="0.5">` (any of the three, '
                    .. "`color` also as `rgb(r, g, b)`), and `<br/>` for a line break. Tags nest and each closes "
                    .. "its own. Write `&lt;`, `&gt;`, `&amp;`, `&quot;` and `&apos;` for the characters "
                    .. "themselves.\n\n"
                    .. "**A tag that is not understood is drawn as text**, so a typo shows as itself rather "
                    .. "than vanishing. Bold is drawn from the label's one face rather than a bold file, and "
                    .. "italic by leaning it. A `TextInput` shows its text plain whatever this says: its "
                    .. "caret counts characters as typed, tags and all.",
            },
            {
                Name = "TextScaled",
                Type = "boolean",
                Default = "false",
                Doc = "Whether `TextSize` is ignored and the text is rasterized at whatever size fills the "
                    .. "box. Re-rasterized rather than scaled: there is no distance field in v1, and a "
                    .. 'stretched bitmap is what "scaled text" usually means and looks like.',
            },
        } :: { Schema.Property },
    },
    {
        Name = "TextButton",
        Extends = "TextLabel",
        Module = "ui",
        Doc = "A `TextLabel` that is meant to be pressed. It adds no property: `Activated`, `PointerEntered` "
            .. "and `PointerExited` are on `UIObject` because anything can be pressed, and what makes this a "
            .. "button is that it is the class a reader recognizes as one.",
    },
    {
        Name = "TextInput",
        Extends = "TextLabel",
        Module = "ui",
        Storage = "TextInput",
        Doc = "A single-line editable field (§2.2). Typed text, a caret you can move with the arrows, Home "
            .. "and End, and both kinds of delete; no selection, no clipboard, no undo and no IME "
            .. "composition beyond what the platform delivers as text.\n\n**The caret starts at the end "
            .. "and goes back there whenever a script assigns `Text`**, which is the only position that is "
            .. "always in range: a caret left where it was would point into a string that no longer exists. "
            .. "Clicking does not yet place it -- that needs the glyph advances the layout produced, and "
            .. "the layout is a different pass.",
        Properties = {
            {
                Name = "PlaceholderText",
                Type = "string",
                Default = '""',
                Doc = "Drawn in place of `Text` while it is empty and the field is unfocused.",
            },
        } :: { Schema.Property },
        Events = {
            {
                Name = "Focused",
                Parameters = {},
                Doc = "Fired when this field starts receiving keystrokes. Focus is taken by a press and "
                    .. "released by a press elsewhere; there is deliberately no settable `Focused` property "
                    .. "beside this, because two fields could then both believe they had it.",
            },
            {
                Name = "FocusLost",
                Parameters = { { Name = "submitted", Type = "boolean" } },
                Doc = "Fired when it stops. `submitted` is true when the field was left by pressing Return "
                    .. 'rather than by clicking away -- the difference between "the player finished" and '
                    .. '"the player went somewhere else".'
                    .. "\n\n**The argument is "
                    .. "not delivered yet**: this release raises the event with no arguments, so a handler's "
                    .. "`submitted` arrives as nil whichever way the field was left. Treat the event as the "
                    .. "fact that focus ended until it carries the distinction.",
            },
        } :: { Schema.Event },
    },
    {
        Name = "ImageLabel",
        Extends = "UIObject",
        Module = "ui",
        Storage = "ImageLabel",
        Doc = "A picture in a box (§2.2).",
        Properties = {
            {
                Name = "Image",
                Type = "Content",
                ContentKind = "Texture",
                Default = '""',
                Doc = "The picture, as an `asset://` URI. A compiled texture out of a pack or an ordinary "
                    .. "PNG out of a source tree; both work, because dev mode is the mode people spend their "
                    .. "time in.\n\nEmpty draws nothing but still fills the background. A URI that names "
                    .. "nothing, and a picture that has been asked for but has not finished loading, both draw "
                    .. "as a flat `ImageColor` rectangle -- the same thing, because from a frame's point of "
                    .. "view they are.",
            },
            {
                Name = "ImageColor",
                Type = "Color3",
                Default = "Color3.new(1, 1, 1)",
                Doc = "Multiplied into the image, so one white glyph sheet can be drawn in any colour. White "
                    .. "leaves it alone.",
            },
            {
                Name = "ScaleType",
                Type = "Enum.ScaleType",
                Default = "Enum.ScaleType.Stretch",
                Doc = "How the picture fits the box.\n\n`Tile` is BOUNDED: past about four thousand "
                    .. "tiles the picture stretches instead, because a one-pixel image tiled over a full-screen "
                    .. "frame is nearly a million quads and the frame that built them would be the hitch. A "
                    .. "visibly wrong picture is one somebody can find; a frame that stops responding is not.",
            },
            {
                Name = "SliceCenter",
                Type = "Rect",
                Default = "Rect.new(Vector2.zero, Vector2.zero)",
                Doc = "For `ScaleType.Slice`, where the nine-slice cuts are, in SOURCE pixels. The four "
                    .. "corners are drawn at their own size whatever the box is, which is how a panel keeps "
                    .. "its rounded corners.\n\nA box narrower than its own two corners collapses the "
                    .. "middle rather than drawing the corners on top of each other.\n\nAn inverted or empty rectangle is kept as given rather than "
                    .. "corrected: a slice quietly fixed up is a panel that draws wrong with no way to find out "
                    .. "why.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "ImageButton",
        Extends = "ImageLabel",
        Module = "ui",
        Doc = "An `ImageLabel` that is meant to be pressed, and the same argument `TextButton` carries: it "
            .. "adds nothing, and it is the class a reader recognizes.",
    },
    {
        Name = "ScrollFrame",
        Extends = "UIObject",
        Module = "ui",
        Storage = "ScrollFrame",
        Doc = "A `Frame` whose contents can be larger than it is (§2.2). It clips its descendants whatever "
            .. "`ClipsDescendants` says -- a scrolling region that did not clip would not be one.",
        Properties = {
            {
                Name = "CanvasSize",
                Type = "UDim2",
                Default = "UDim2.new()",
                Doc = "How big the scrollable area is. Children lay out against this rather than against the "
                    .. "frame's own size.",
            },
            {
                Name = "CanvasPosition",
                Type = "Vector2",
                Default = "Vector2.zero",
                Doc = "How far the canvas is scrolled, in pixels. Clamped to what the canvas allows on the "
                    .. "next layout, so a write past the end settles at the end rather than showing "
                    .. "emptiness.",
            },
            {
                Name = "ScrollBarThickness",
                Type = "number",
                Default = "12",
                Doc = "In pixels; zero draws no bar and still scrolls, which is what a touch surface wants."
                    .. "\n\nA bar appears only on an axis that can actually move: a canvas that fits its "
                    .. "region gets none, because a bar for an axis with nowhere to go is a bar that lies. The "
                    .. "thumb is as long a fraction of the track as the view is of the canvas, and never "
                    .. "shorter than the bar is wide -- a thumb of two pixels in a very long canvas is a thumb "
                    .. "nobody can grab.\n\nBoth parts take their colour from the frame's own "
                    .. "`BackgroundColor`, at a quarter and three quarters alpha. v1 has no theme, and a "
                    .. "hard-coded grey would be wrong on half of them.\n\nThe bar is drawn OUTSIDE "
                    .. "the region's own clip, or it would scroll away with the content it reports on.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "UIListLayout",
        Extends = "Instance",
        Module = "ui",
        Parents = InAUi,
        Storage = "UIListLayout",
        Doc = "Stacks its parent's `UIObject` children in a line (§2.2). A modifier rather than a container: "
            .. "it is parented BESIDE the things it arranges, so adding one to a Frame does not reparent "
            .. "anything.\n\nA child laid out by one keeps its `Size` and loses its `Position` -- the layout "
            .. "decides where each one goes, which is what a layout is.",
        Properties = {
            {
                Name = "FillDirection",
                Type = "Enum.FillDirection",
                Default = "Enum.FillDirection.Vertical",
                Doc = "Which way the line runs.",
            },
            {
                Name = "Padding",
                Type = "UDim",
                Default = "UDim.new()",
                Doc = "The gap BETWEEN children, not around them -- `UIPadding` is what puts space around the "
                    .. "outside. Scale is a fraction of the parent along the fill direction.",
            },
            {
                Name = "HorizontalAlignment",
                Type = "Enum.HorizontalAlignment",
                Default = "Enum.HorizontalAlignment.Left",
                Doc = "Where the line sits across the parent.",
            },
            {
                Name = "VerticalAlignment",
                Type = "Enum.VerticalAlignment",
                Default = "Enum.VerticalAlignment.Top",
                Doc = "Where it sits down the parent.",
            },
            {
                Name = "SortOrder",
                Type = "Enum.SortOrder",
                Default = "Enum.SortOrder.LayoutOrder",
                Doc = "What order the children go in.",
            },
            {
                Name = "Wraps",
                Type = "boolean",
                Default = "false",
                Doc = "Whether the line breaks into a second one when it runs out of room, rather than "
                    .. "overflowing.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "UIPadding",
        Extends = "Instance",
        Module = "ui",
        Parents = InAUi,
        Storage = "UIPadding",
        Doc = "Insets its parent's content on each side (§2.2). Like `UIListLayout`, a modifier parented "
            .. "beside what it affects.",
        Properties = {
            {
                Name = "PaddingTop",
                Type = "UDim",
                Default = "UDim.new()",
                Doc = "Scale is a fraction of the parent's own height.",
            },
            {
                Name = "PaddingBottom",
                Type = "UDim",
                Default = "UDim.new()",
                Doc = "Scale is a fraction of the parent's own height.",
            },
            {
                Name = "PaddingLeft",
                Type = "UDim",
                Default = "UDim.new()",
                Doc = "Scale is a fraction of the parent's own width.",
            },
            {
                Name = "PaddingRight",
                Type = "UDim",
                Default = "UDim.new()",
                Doc = "Scale is a fraction of the parent's own width.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "UICorner",
        Extends = "Instance",
        Module = "ui",
        Parents = under("UIObject"),
        Storage = "UICorner",
        Doc = "Rounds its parent's corners (§2.2). It changes the DRAWING and not the layout or the hit test: "
            .. "a rounded button is still a rectangle to the solver and to the pointer, which is what keeps "
            .. "a corner radius from being a geometry problem.",
        Properties = {
            {
                Name = "CornerRadius",
                Type = "UDim",
                Default = "UDim.new(0, 8)",
                Doc = "Scale is a fraction of the parent's SHORTER side, so a pill shape is "
                    .. "`UDim.new(0.5, 0)` whatever the box's proportions.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "AudioGroup",
        Extends = "Instance",
        Module = "audio",
        Storage = "AudioGroup",
        Doc = "A mixing bus (§2.1, §2.2). A `Sound` whose `Group` names one has its volume multiplied by the "
            .. 'group\'s, which is how "turn the music down" is one property rather than a loop over every '
            .. "music track.\n\nNamed `AudioGroup` rather than `SoundGroup` because "
            .. "the service is `AudioService`, and one prefix across a family beats two.",
        Properties = {
            {
                Name = "Volume",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "A multiplier, not a level in decibels: 0 is silent, 1 is unchanged, and above 1 is "
                    .. "louder and will clip if the material was already near full scale. Nesting is not "
                    .. "supported -- a group parented to a group is just an instance parented to an instance, "
                    .. "and only a `Sound`'s own `Group` is read.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "Sound",
        Extends = "Instance",
        Module = "audio",
        Storage = "Sound",
        Doc = "One sound, playing or not (§2.2). **Parent it to a `BasePart` and it is positional**; parent it "
            .. "anywhere else and it is 2D. That is the whole of the 3D switch, and it is a property of where "
            .. "the instance sits rather than a flag that could disagree with it.\n\n**Positional means two "
            .. "things**: it gets quieter with distance, and it comes out of the side it is on. The side is an "
            .. "azimuth in the listener's own frame, so turning around swaps it -- elevation is not expressed, "
            .. "because two channels cannot, and a source directly ahead and one directly behind are both centre "
            .. "for the same reason. A positional sound is folded to mono before it is placed; a 2D one keeps its "
            .. "own stereo image, which is all it has.\n\n**Its timeline is the "
            .. "SimClock's, not the mixer's.** `TimePosition` advances by "
            .. "`FixedTimestep * PlaybackSpeed` per tick and `Ended` fires from that timeline, so a replay "
            .. "reproduces both exactly and a headless run with no audio device produces the same `Ended` on "
            .. "the same tick as a run with speakers. What the speakers do is downstream of the simulation and "
            .. "never an input to it -- otherwise a script reading `TimePosition` would be reading the wall "
            .. "clock through a side door (R10).\n\n**How long it is, is the FILE's.** `TimePosition` "
            .. "advances on the simulation's clock and stops at the length of what `Content` decoded to, so "
            .. "the two facts a sound is made of come from the two places that own them.",
        Properties = {
            {
                Name = "Content",
                Type = "Content",
                ContentKind = "Audio",
                Default = '""',
                Doc = "The audio, as an `asset://` URI. WAV, MP3, FLAC and Ogg Vorbis. A file of up to "
                    .. "ten seconds is decoded once and held; a longer one STREAMS -- its encoded bytes "
                    .. "are kept and each voice decodes just ahead of the speakers, so three minutes of "
                    .. "music costs the file's size rather than about 70 MB. Either way a sound does not "
                    .. "re-read its file sixty times a second.\n\nA URI that "
                    .. "names nothing still PLAYS, as a generated tone whose pitch comes from a hash of the "
                    .. "id. A sound that went silent because a file was missing is a bug report about the "
                    .. "sound; a placeholder that is audibly a placeholder is one about the file. "
                    .. '`DebugService:GetStat("AudioClipsMissing")` is how a script asks which it '
                    .. "got.\n\n**The file's length is the sound's length**, read from its header "
                    .. "rather than by decoding it. `Ended` fires when "
                    .. "`TimePosition` reaches it, so a two-minute track plays for two minutes; a "
                    .. "content that names nothing is one second long, because that is how long the "
                    .. "placeholder tone is.\n\nThe mixer keeps its own cursor and takes "
                    .. "`TimePosition` whenever the two part company -- a seek, a rewind, a sound "
                    .. "stopped and started. It has to: the timeline moves in whole ticks and a "
                    .. "device moves in buffers, so a cursor re-read every frame stutters between "
                    .. "them. Nothing in the simulation reads that cursor, which is what keeps a "
                    .. "replay exact (R10).",
            },
            {
                Name = "Playing",
                Type = "boolean",
                Default = "false",
                Doc = "Whether the timeline is advancing. Writing it is the same as calling `Play` or `Stop`, "
                    .. "and reading it is how a script asks without keeping its own flag.",
            },
            {
                Name = "Looped",
                Type = "boolean",
                Default = "false",
                Doc = "Whether reaching the end wraps to the start instead of stopping. A looped sound never "
                    .. "fires `Ended`, because it never does.",
            },
            {
                Name = "Volume",
                Type = "number",
                Default = "0.5",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "A multiplier, combined with the group's and with the listener distance. The default is "
                    .. "half rather than full because a game with several sounds at once and every one at 1 is "
                    .. "a game that clips, and the first thing anybody does is turn them all down.",
            },
            {
                Name = "PlaybackSpeed",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "How fast the timeline runs, and therefore the pitch. 2 is an octave up and half the "
                    .. "duration. Zero is refused rather than treated as a pause -- `Playing` is what pauses, "
                    .. "and a speed of zero would be a sound that never ends.",
            },
            {
                Name = "TimePosition",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_at_least_zero",
                Transient = true,
                Doc = "Where the timeline is, in seconds, from 0 to `TimeLength`. Writable, which is how a "
                    .. "script seeks: a write below zero is refused, and one past the end is clamped to it -- "
                    .. "or wrapped, for a `Looped` sound -- exactly as arriving there would.",
            },
            {
                Name = "TimeLength",
                Type = "number",
                ReadOnly = true,
                Transient = true,
                Doc = "How long `Content` is, in seconds: the file's own length, read from its header, or the "
                    .. "placeholder tone's one second when it names nothing. 0 until the length has been read: "
                    .. "the world's first tick after `Content` is set reads it, and the editor reads it for the "
                    .. "sound it shows.",
            },
            {
                Name = "RollOffMinDistance",
                Type = "number",
                Default = "8",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How far the listener can be before a positional sound starts getting quieter. Inside "
                    .. "it the sound is at full `Volume`; ignored entirely for a 2D sound.",
            },
            {
                Name = "RollOffMaxDistance",
                Type = "number",
                Default = "80",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How far the listener can be before a positional sound is silent. Between the two "
                    .. "distances it fades linearly -- linear rather than inverse-square because a game's "
                    .. "audible range is a design decision rather than a physical one, and an inverse square "
                    .. "makes the far half of it inaudible.",
            },
            {
                Name = "Group",
                Type = "AudioGroup?",
                Default = "nil",
                Doc = "The bus this sound's volume is multiplied by, or nil for none.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "Play",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Plays it from the start -- or from `TimePosition`, when a script set it since the "
                    .. "sound last started. Playing one that is already playing starts it again. `Resume` "
                    .. "is what carries on from where `Pause` left it.",
            },
            {
                Name = "Resume",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Carries on from `TimePosition`, which is where `Pause` left it. The same as "
                    .. "`Playing = true`.",
            },
            {
                Name = "Pause",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Stops the timeline where it is. `Resume` carries on from there; `Play` starts "
                    .. "again from the start. The same as `Playing = false`.",
            },
            {
                Name = "Stop",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Stops it and rewinds to the start. `Ended` does NOT fire: it is a past-tense fact about "
                    .. "reaching the end, and code that awards something when a jingle finishes must not be "
                    .. "fooled by one that was cut off.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "Ended",
                Parameters = {},
                Doc = "Fired on the tick the timeline reaches the end of a sound that is not `Looped`, which "
                    .. "stops it and rewinds `TimePosition` to 0 -- so playing it again plays it again. From "
                    .. "the SIM timeline rather than from the mixer, so it lands on the same tick in a replay, "
                    .. "in a headless run, and on a machine whose audio buffer is four times the size.",
            },
            {
                Name = "Loaded",
                Parameters = {},
                Doc = "Fired when this sound's `Content` has been decoded and is ready to play. **Fires "
                    .. "immediately in v1**, because a sound has nothing to wait for yet -- "
                    .. "declared now so that code written today does not have to change when it does.",
            },
        } :: { Schema.Event },
    },
    {
        Name = "AnimationPlayer",
        Extends = "Instance",
        Module = "render",
        Parents = under("Model"),
        Doc = "Plays a skinned mesh's animation clips (§2.2).\n\n**Parent it to the `Model` "
            .. "whose character it is, and it drives every skinned `MeshPart` under that model.** A "
            .. "character is a body, a shirt and a pair of trousers -- several meshes wearing the same "
            .. "skeleton -- and one clip has to move all of them. Only one of the pieces needs to carry "
            .. "the animation; the clip is taken from the first that does, in tree order.\n\n"
            .. "**The joints are matched by NAME across the pieces, never by index.** Two files exported "
            .. "separately wear the same skeleton in the sense that matters -- the same joints, named "
            .. "the same -- and in no other: an exporter is free to order them differently, and a clip "
            .. "applied through the wrong index twists a sleeve in a way that looks like a broken "
            .. "animation rather than a mismatched rig. A joint one piece does not have is skipped, so a "
            .. "shirt with no fingers keeps its own sleeve.\n\nParenting it straight to a "
            .. "`MeshPart` still works and drives exactly that mesh, which is what a character made of "
            .. "one piece wants.\n\nIt stores nothing: the tracks are the "
            .. "state, and each one is a handle a script holds rather than a child in the tree. Sampling "
            .. "happens at `PreAnimation` on the SimClock, so a clip's position at a given tick is the same "
            .. "in a replay as it was live.\n\nv1 is clip playback and linear blending -- no state machines, "
            .. "no IK, and no root motion.",
        Methods = {
            {
                Name = "LoadAnimation",
                Parameters = { { Name = "content", Type = "Content" } },
                Returns = { "AnimationTrack" },
                ThreadSafety = "Unsafe",
                Doc = "A track for one clip. Everything after a `#` is the clip's NAME inside the file, and a "
                    .. "string with no `#` is a clip name in this player's own mesh -- so the common case is "
                    .. '`player:LoadAnimation("Walk")`.\n\n'
                    .. "**A path before the `#` names the file the CLIP is in, and it need not be the "
                    .. "one this player's skeleton came from.** One walk cycle authored once and played "
                    .. "by every character is the reason a clip is addressable at all, and a clip from "
                    .. "elsewhere is retargeted onto this rig by joint NAME. A joint the target does not "
                    .. "have is skipped rather than guessed, so a clip for a horse played on a person "
                    .. "moves the joints they have in common and no others -- and a file nothing has "
                    .. "loaded gives the same empty track a clip name the file lacks does.\n\n**Load a track "
                    .. "once and keep it.** It always returns a track, even for a clip that is not there -- a "
                    .. "mesh that has not finished loading would otherwise make an ordinary frame a nil index "
                    .. "-- and every call is a handle the VM holds until the world goes away.",
            },
        } :: { Schema.Method },
    },
    {
        Name = "Part2D",
        Extends = "Instance",
        Module = "scene",
        Parents = under("Workspace", "Model", "Part2D"),
        Storage = "Part2D",
        Doc = "A sprite and a body in one, on the 2D plane (the 2D layer, phase 3): what a `Part` is to a 3D "
            .. "world. It lies on the world's XY plane facing -Z, so an orthographic `Camera` looking down -Z "
            .. "sees it as it is; `ZIndex` orders sprites that overlap. It is drawn as its `Image` -- or a flat "
            .. "`Color` without one -- and simulated by 2D physics unless `Anchored`, which makes it ground.",
        Properties = {
            { Name = "Position", Type = "Vector2", Doc = "Where its middle is, in metres." },
            {
                Name = "Rotation",
                Type = "number",
                Doc = "Degrees, counter-clockwise. A part with `FixedRotation` keeps whatever this is.",
            },
            {
                Name = "Size",
                Type = "Vector2",
                Default = "Vector2.new(1, 1)",
                ErrorKey = "scene.err.size2_positive",
                Doc = "Width and height in metres: the picture's and the collider's.",
            },
            { Name = "Shape", Type = "Enum.Shape2D", Doc = "The outline it is drawn and collides as." },
            {
                Name = "Color",
                Type = "Color3",
                Default = "Color3.new(1, 1, 1)",
                Doc = "Multiplies the image; with no image, the colour it is drawn in.",
            },
            {
                Name = "Transparency",
                Type = "number",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "0 is as drawn, 1 is invisible. A transparent part still collides.",
            },
            {
                Name = "ZIndex",
                Type = "number",
                Doc = "Which of two overlapping sprites is in front: the higher. Ties keep the tree's order.",
            },
            {
                Name = "FlipX",
                Type = "boolean",
                Doc = "Mirrors the picture left to right: a character facing the other way.",
            },
            { Name = "FlipY", Type = "boolean", Doc = "Mirrors the picture top to bottom." },
            {
                Name = "Image",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "The picture, stretched over `Size`. Its transparent pixels are transparent.",
            },
            {
                Name = "ImageRectOffset",
                Type = "Vector2",
                Doc = "The top-left of the part of the image drawn, in pixels: one frame of a sprite sheet.",
            },
            {
                Name = "ImageRectSize",
                Type = "Vector2",
                Doc = "The size of that part in pixels. Zero on both axes draws the whole image.",
            },
            {
                Name = "Filter",
                Type = "Enum.TextureFilter",
                Doc = "Linear for painted art, Nearest for pixel art.",
            },
            {
                Name = "Anchored",
                Type = "boolean",
                Doc = "Ground: it does not move, and what lands on it stays. A script may still move it.",
            },
            {
                Name = "CanCollide",
                Type = "boolean",
                Default = "true",
                Doc = "Whether other parts bump into it. A part that cannot collide is only a picture.",
            },
            {
                Name = "Sensor",
                Type = "boolean",
                Doc = "Reports what enters it through `Touched` and pushes nothing: a coin, a checkpoint, a "
                    .. "trigger.",
            },
            {
                Name = "Density",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "Kilograms per square metre: with `Size`, how heavy it is.",
            },
            {
                Name = "Friction",
                Type = "number",
                Default = "0.3",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How much it grips what it slides on. Zero is ice.",
            },
            {
                Name = "Elasticity",
                Type = "number",
                ErrorKey = "scene.err.number_zero_to_one",
                Doc = "How much of its speed a bounce gives back: 0 lands dead, 1 bounces for ever.",
            },
            {
                Name = "FixedRotation",
                Type = "boolean",
                Doc = "Never turns, whatever hits it: a character that stays upright.",
            },
            {
                Name = "GravityScale",
                Type = "number",
                Default = "1",
                Doc = "How strongly `Workspace.Gravity` pulls it: 0 floats, 2 falls twice as hard, and a "
                    .. "negative number falls up.",
            },
            {
                Name = "Velocity",
                Type = "Vector2",
                Doc = "Metres per second. Written by the simulation every tick, and a script may set it: a jump "
                    .. "is setting its y.",
            },
            {
                Name = "AngularVelocity",
                Type = "number",
                Doc = "Degrees per second, counter-clockwise.",
            },
            {
                Name = "CollisionGroup",
                Type = "string",
                Default = '"Default"',
                Doc = "The group deciding which other parts it collides with, as `PhysicsService` names them.",
            },
        },
        Methods = {
            {
                Name = "ApplyImpulse",
                Parameters = { { Name = "impulse", Type = "Vector2" } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "A sudden push, in kilogram-metres per second, at its middle. Applied at the next tick; an "
                    .. "anchored part ignores it.",
            },
        } :: { Schema.Method },
        Events = {
            {
                Name = "Touched",
                Parameters = { { Name = "otherPart", Type = "Instance" } },
                Doc = "Fires once when something begins touching it -- another `Part2D`, or a `Tilemap2D`'s "
                    .. "solid tiles -- or, for a `Sensor`, begins overlapping it. Deferred like every signal.",
            },
            {
                Name = "TouchEnded",
                Parameters = { { Name = "otherPart", Type = "Instance" } },
                Doc = "Fires when something that was touching it stops.",
            },
        } :: { Schema.Event },
    },
    {
        Name = "Tilemap2D",
        Extends = "Instance",
        Module = "scene",
        Parents = under("Workspace", "Model"),
        Storage = "Tilemap2D",
        Doc = "A grid of tiles from one tileset image, on the 2D plane (the 2D layer, phase 3): a level you "
            .. "paint. Tile 0 is empty; tile `n` is the `n`-th tile of the tileset, counted along its rows "
            .. "from the top left. Every tile that is not empty is solid when `Collides` is on, as one body "
            .. "whose edges are merged, so a character runs across a floor of tiles without catching on their "
            .. "seams.",
        Properties = {
            {
                Name = "Position",
                Type = "Vector2",
                Doc = "Where the bottom-left corner of cell (0, 0) is, in metres.",
            },
            {
                Name = "CellSize",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "The side of one cell, in metres.",
            },
            {
                Name = "Tileset",
                Type = "Content",
                ContentKind = "Texture",
                Doc = "The image the tiles are cut from.",
            },
            {
                Name = "TileSize",
                Type = "Vector2",
                Default = "Vector2.new(16, 16)",
                ErrorKey = "scene.err.size2_positive",
                Doc = "One tile's size in the tileset, in pixels.",
            },
            {
                Name = "ZIndex",
                Type = "number",
                Doc = "Its place among overlapping sprites, as a `Part2D`'s.",
            },
            {
                Name = "Color",
                Type = "Color3",
                Default = "Color3.new(1, 1, 1)",
                Doc = "Multiplies every tile.",
            },
            {
                Name = "Filter",
                Type = "Enum.TextureFilter",
                Default = "Enum.TextureFilter.Nearest",
                Doc = "Nearest by default, because tiles are nearly always pixel art.",
            },
            {
                Name = "Collides",
                Type = "boolean",
                Default = "true",
                Doc = "Whether its tiles are solid. Off, it is scenery: a background layer.",
            },
            {
                Name = "Friction",
                Type = "number",
                Default = "0.3",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How much its tiles grip what slides on them.",
            },
            {
                Name = "CollisionGroup",
                Type = "string",
                Default = '"Default"',
                Doc = "The group its tiles collide as.",
            },
        },
        Methods = {
            {
                Name = "SetCell",
                Parameters = {
                    { Name = "x", Type = "number" },
                    { Name = "y", Type = "number" },
                    { Name = "tile", Type = "number" },
                },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Puts tile `tile` in cell (`x`, `y`); 0 empties it. Cells are whole numbers, and y grows "
                    .. "upwards, as the world's does.",
            },
            {
                Name = "GetCell",
                Parameters = { { Name = "x", Type = "number" }, { Name = "y", Type = "number" } },
                Returns = { "number" },
                ThreadSafety = "ReadParallel",
                Doc = "The tile in cell (`x`, `y`), or 0.",
            },
            {
                Name = "FillRect",
                Parameters = {
                    { Name = "x0", Type = "number" },
                    { Name = "y0", Type = "number" },
                    { Name = "x1", Type = "number" },
                    { Name = "y1", Type = "number" },
                    { Name = "tile", Type = "number" },
                },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Puts `tile` in every cell of the rectangle from (`x0`, `y0`) to (`x1`, `y1`), both "
                    .. "corners included.",
            },
            {
                Name = "Clear",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Empties every cell.",
            },
        } :: { Schema.Method },
    },
    {
        Name = "Constraint2D",
        Extends = "Instance",
        Module = "scene",
        Parents = under("Workspace", "Model", "Part2D"),
        Storage = "Constraint2D",
        Tags = { "Abstract", "NotCreatable" } :: { Schema.ClassTag },
        Doc = "The base of a joint between two `Part2D`s (ADR 0102): the 2D solver holds them together, "
            .. "and a rope bridge, a door on a hinge or a swinging lamp is what comes of it.\n\n"
            .. "**Two parts and a point on each**, not two attachments: a 2D joint is a point, and each "
            .. "part's is in that part's own metres from its middle. A joint that cannot hold -- a part that "
            .. "is missing or is not in the world, or two anchored parts -- holds nothing and is not an "
            .. "error.",
        Properties = {
            {
                Name = "Part0",
                Type = "Part2D?",
                Doc = "One of the two parts it holds.",
            },
            {
                Name = "Part1",
                Type = "Part2D?",
                Doc = "The other.",
            },
            {
                Name = "Anchor0",
                Type = "Vector2",
                Doc = "Where the joint is on `Part0`, in metres from its middle, in its own frame, so it turns "
                    .. "with the part.",
            },
            {
                Name = "Anchor1",
                Type = "Vector2",
                Doc = "Where the joint is on `Part1`, the same way.",
            },
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether the joint holds. A disabled joint stays in the world holding nothing.",
            },
            {
                Name = "CollideConnected",
                Type = "boolean",
                Default = "false",
                Doc = "Whether the two parts still collide with each other. False is what a chain of links "
                    .. "or a limb wants, where the parts overlap at the joint by construction.",
            },
        },
    },
    {
        Name = "HingeConstraint2D",
        Extends = "Constraint2D",
        Module = "scene",
        -- A hook of its own that stamps the kind onto the one `Constraint2D`
        -- component, as the 3D constraints do: the type is the class.
        Storage = "HingeConstraint2D",
        Doc = "A point both parts turn about: a door, a wheel, a pendulum, a lever. With `Anchor0` and "
            .. "`Anchor1` at the same place in the world, the parts stay together there and turn freely, "
            .. "limited or driven as below.",
        Properties = {
            {
                Name = "LimitsEnabled",
                Type = "boolean",
                Default = "false",
                Doc = "Whether `LowerAngle` and `UpperAngle` apply. Off turns freely.",
            },
            {
                Name = "LowerAngle",
                Type = "number",
                Default = "-90",
                Doc = "How far `Part1` may turn clockwise, in degrees, from how the two stood when the joint "
                    .. "was made.",
            },
            {
                Name = "UpperAngle",
                Type = "number",
                Default = "90",
                Doc = "How far it may turn counter-clockwise, the same way.",
            },
            {
                Name = "MotorEnabled",
                Type = "boolean",
                Default = "false",
                Doc = "Whether the hinge drives itself towards `MotorSpeed`: a wheel, a fan, a windmill.",
            },
            {
                Name = "MotorSpeed",
                Type = "number",
                Default = "0",
                Doc = "The speed the motor drives to, in degrees per second, counter-clockwise positive.",
            },
            {
                Name = "MotorMaxTorque",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The most torque the motor applies, in newton metres. What stops it is anything that "
                    .. "needs more.",
            },
        },
    },
    {
        Name = "SpringConstraint2D",
        Extends = "Constraint2D",
        Module = "scene",
        Storage = "SpringConstraint2D",
        Doc = "A distance between two points, held softly: a spring, a bungee, a suspension. At a "
            .. "`Stiffness` of 0 it is a rigid rod of `Length`, which is what a rope bridge's links are.",
        Properties = {
            {
                Name = "Length",
                Type = "number",
                Default = "2",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "The rest length, in metres.",
            },
            {
                Name = "Stiffness",
                Type = "number",
                Default = "4",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "How quickly it springs back, in hertz. 0 makes it rigid.",
            },
            {
                Name = "Damping",
                Type = "number",
                Default = "0.5",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The damping ratio: 0 bounces for ever, 1 settles without overshooting.",
            },
            {
                Name = "MinLength",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The shortest it may be squeezed to, in metres, however hard it is pushed.",
            },
            {
                Name = "MaxLength",
                Type = "number",
                Default = "1000",
                ErrorKey = "scene.err.number_at_least_zero",
                Doc = "The longest it may be stretched to, in metres: the end of a bungee.",
            },
        },
    },
    {
        Name = "WeldConstraint2D",
        Extends = "Constraint2D",
        Module = "scene",
        Storage = "WeldConstraint2D",
        Doc = "The two parts held rigidly together, in the placement they had when the weld was made: "
            .. "a sword in a hand, a crate's lid, a vehicle's body on its frame.",
    },
    {
        Name = "SpriteAnimator",
        Extends = "Instance",
        Module = "scene",
        Parents = under("Part2D"),
        Storage = "SpriteAnimator",
        Doc = "Plays frames of its parent `Part2D`'s sprite sheet on the simulation clock (ADR 0102), so a "
            .. "walk cycle is data instead of script. Each tick it is playing, it writes the parent's "
            .. "`ImageRectOffset` and `ImageRectSize` for the frame it is on; the picture, the undo and the "
            .. "wire all see that as they see any write.\n\n"
            .. "Frames are counted along the sheet's rows from `SheetOffset`, `Columns` to a row. A "
            .. "non-looped animation that reaches its end stops and writes `Playing = false`, which is its "
            .. 'end event: listen with `GetPropertyChangedSignal("Playing")`.',
        Properties = {
            {
                Name = "FrameSize",
                Type = "Vector2",
                Default = "Vector2.new(16, 16)",
                ErrorKey = "scene.err.size2_positive",
                Doc = "One frame's size in the sheet, in pixels.",
            },
            {
                Name = "Columns",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.whole_number_at_least_one",
                Doc = "How many frames make one row of the sheet.",
            },
            {
                Name = "SheetOffset",
                Type = "Vector2",
                Doc = "Where frame 0 starts, in pixels from the sheet's top left, so one sheet can hold several "
                    .. "animations one under another.",
            },
            {
                Name = "FirstFrame",
                Type = "number",
                Default = "0",
                ErrorKey = "scene.err.whole_number_at_least_zero",
                Doc = "The frame this animation starts on, counted from `SheetOffset`.",
            },
            {
                Name = "FrameCount",
                Type = "number",
                Default = "1",
                ErrorKey = "scene.err.whole_number_at_least_one",
                Doc = "How many frames it plays.",
            },
            {
                Name = "FramesPerSecond",
                Type = "number",
                Default = "8",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "How fast, on the simulation clock.",
            },
            {
                Name = "Looped",
                Type = "boolean",
                Default = "true",
                Doc = "Whether it starts over at the end. Off stops on the last frame.",
            },
            {
                Name = "Playing",
                Type = "boolean",
                Default = "false",
                Doc = "Whether it advances. Setting it true starts from `FirstFrame` when it had stopped at the "
                    .. "end, and carries on from where it was when it had been paused.",
            },
            {
                Name = "Frame",
                Type = "number",
                ReadOnly = true,
                Doc = "The frame it is on, from 0 at `FirstFrame`.",
            },
        },
    },
    {
        Name = "NavigationArea",
        Extends = "Instance",
        Module = "scene",
        Storage = "NavigationArea",
        Parents = under("BasePart"),
        Doc = "Marks the walkable ground inside its part's box with a `Label` (ADR 0098), which "
            .. "`NavigationService:SetAreaCost` prices: water an agent would rather go round, mud that is slow, "
            .. "a door that is shut. The part need not collide or be anchored -- it is a volume, not a floor. "
            .. "Ground no area marks costs 1.",
        Properties = {
            {
                Name = "Label",
                Type = "string",
                Default = '"Area"',
                Doc = "The name its cost is set by. Areas that share a label share a cost.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "NavigationLink",
        Extends = "Instance",
        Module = "scene",
        Storage = "NavigationLink",
        Doc = "A way between two points the walkable ground does not join (ADR 0098): a gap to jump, a "
            .. "ladder, a drop. A path may use it; `FindPath`'s third answer names its `Label` at the waypoint "
            .. "where it begins, so the script walking the path knows to jump there rather than walk. Both "
            .. "points are in the world, and each must be near walkable ground.",
        Properties = {
            {
                Name = "From",
                Type = "vector",
                Doc = "Where it starts, in the world.",
            },
            {
                Name = "To",
                Type = "vector",
                Doc = "Where it ends, in the world.",
            },
            {
                Name = "Bidirectional",
                Type = "boolean",
                Default = "true",
                Doc = "Whether it can be taken back from `To` to `From`: a ladder can, a drop cannot.",
            },
            {
                Name = "Label",
                Type = "string",
                Default = '"Jump"',
                Doc = "What crossing it is, as the path reports it.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "NavigationAgent",
        Extends = "Instance",
        Module = "scene",
        Storage = "NavigationAgent",
        Parents = under("BasePart"),
        Doc = "Walks its part to a point over the walkable ground, keeping clear of the other agents as it "
            .. "goes -- a crowd (ADR 0098). The engine moves the part on the simulation clock: set `Target`, "
            .. "and `Reached` fires when it arrives. For one character a script walking `FindPath`'s waypoints "
            .. "is enough; this is for many that must not walk through each other.",
        Properties = {
            {
                Name = "Target",
                Type = "vector",
                Doc = "Where it is going. Writing it starts the walk; while `Active` is false it stands still.",
            },
            {
                Name = "Active",
                Type = "boolean",
                Default = "false",
                Doc = "Whether it is walking. Writing `Target` sets it; arriving clears it.",
            },
            {
                Name = "MaxSpeed",
                Type = "number",
                Default = "8",
                ErrorKey = "scene.err.number_above_zero",
                Doc = "Its fastest, in metres per second.",
            },
            {
                Name = "AgentType",
                Type = "string",
                Default = '""',
                Doc = "Which agent it is, as `NavigationService:DefineAgent` named it; empty for the service's own.",
            },
        } :: { Schema.Property },
        Events = {
            {
                Name = "Reached",
                Parameters = {},
                Doc = "Fired once when it arrives at `Target`.",
            },
        } :: { Schema.Event },
    },
}

local Definition: Schema.Definition = {
    Classes = Classes,
}

return Definition
