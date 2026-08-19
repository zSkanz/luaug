--!strict
--- Instance classes (api-design.md §2.2).
---
--- The M2 surface only: `Instance` and the classes a script can build a tree
--- out of. Classes whose behaviour belongs to a later milestone arrive with it,
--- because a property that accepts a write and changes nothing is worse than a
--- missing one -- it type-checks.

local Schema = require("../schema")

--- The attribute value domain (§2.2). Written once because it widens as the
--- remaining datatypes ship -- `Vector2`, `UDim`, `UDim2` and `Rect` join it at
--- the milestone that declares them -- and three copies would widen at three
--- different times.
local AttributeValue = "string | number | boolean | vector | CFrame | Color3"
local OptionalAttributeValue = "(" .. AttributeValue .. ")?"
local AttributeMap = "{ [string]: " .. AttributeValue .. " }"

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
        Name = "Folder",
        Extends = "Instance",
        Module = "scene",
        Doc = "A node with no behaviour of its own, for grouping instances; mounting src/scripts also builds one per subdirectory.",
    },
    {
        Name = "Model",
        Extends = "Instance",
        Module = "scene",
        Doc = "A group of parts handled as one object, with a pivot to move it by and an extents box to measure it with.",
        Properties = {
            {
                Name = "PrimaryPart",
                Type = "BasePart?",
                Doc = "The part this model's pivot follows; Clone rewires it to the copy, since it is the reference into a cloned subtree that catches people.",
            },
        },
        Methods = {
            {
                Name = "GetPivot",
                Parameters = {},
                Returns = { "CFrame" },
                Doc = "This model's pivot in world space.",
            },
            {
                Name = "PivotTo",
                Parameters = { { Name = "targetCFrame", Type = "CFrame" } },
                Returns = {},
                Doc = "Moves the model so that its pivot lands on targetCFrame, carrying every descendant part along by the same transform.",
            },
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
        Name = "Script",
        Extends = "Instance",
        Module = "scene",
        Tags = { "NotCreatable" } :: { Schema.ClassTag },
        Doc = "An entry-point Luau file, mounted from src/scripts at boot and started on its own coroutine; modules are required by string and never appear in the tree.",
        Properties = {
            {
                Name = "Enabled",
                Type = "boolean",
                Default = "true",
                Doc = "Whether this script starts at boot; writing it afterwards has no effect in v1, neither stopping a running script nor starting one that did not run, because what sets it acts before the boot it applies to.",
            },
        },
    },
    {
        -- The structural half only (m2-kickoff Decision 6). Anchored,
        -- CanCollide, CanQuery, Material, CollisionGroup, Friction,
        -- Restitution, Density, LinearVelocity, AngularVelocity,
        -- ApplyImpulse, Touched and TouchEnded are promises about a
        -- simulation that does not exist until M5, and a type-checked no-op
        -- looks more like a working API than a missing member does.
        Name = "BasePart",
        Extends = "Instance",
        Module = "scene",
        Tags = { "Abstract" } :: { Schema.ClassTag },
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
                Name = "Color",
                Type = "Color3",
                Doc = "This part's color; channels are not clamped to 0-1, so a value above one is a legal tint rather than an error.",
            },
            {
                Name = "Transparency",
                Type = "number",
                Doc = "How see-through this part is, 0 fully opaque and 1 fully invisible.",
            },
        },
    },
    {
        Name = "Part",
        Extends = "BasePart",
        Module = "scene",
        Doc = "The primitive solid: a part whose geometry is one of a small set of shapes rather than an imported mesh.",
        Properties = {
            {
                Name = "Shape",
                Type = "Enum.PartShape",
                Doc = "Which primitive solid this part is.",
            },
        },
    },
}

local Definition: Schema.Definition = {
    Classes = Classes,
}

return Definition
