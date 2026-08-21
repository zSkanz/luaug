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
        Storage = "Model",
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
        Storage = "Script",
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
        -- The structural half arrived at M2 and the physical half at M5, when
        -- there was a simulation to mean something by it. `Material` is still
        -- absent: it is a surface look rather than rigidbody state, nothing
        -- reads it, and a type-checked no-op looks more like a working API than
        -- a missing member does (m2-kickoff Decision 6, still the rule).
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
                Name = "Color",
                Type = "Color3",
                Doc = "This part's color; channels are not clamped to 0-1, so a value above one is a legal tint rather than an error.",
            },
            {
                Name = "Transparency",
                Type = "number",
                Doc = "How see-through this part is, 0 fully opaque and 1 fully invisible.",
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
        Storage = "MeshPart",
        Doc = "A part whose geometry is an imported mesh rather than a primitive solid. One file is one mesh: a "
            .. "model made of several pieces is several MeshParts, which is the shape §2.6's prefab example already "
            .. "assumes.",
        Properties = {
            {
                Name = "MeshContent",
                Type = "Content",
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
        },
    },
    {
        Name = "Weld",
        Extends = "Instance",
        Module = "scene",
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
                Type = "BasePart?",
                Doc = "The anchor: the part the other one follows. It may itself be simulated, anchored, or a "
                    .. "CharacterBody -- whatever moves it, Part1 goes with it.",
            },
            {
                Name = "Part1",
                Type = "BasePart?",
                Doc = "The driven part. While the weld is active it is moved by the weld and by nothing else: "
                    .. "gravity does not reach it, and writing its CFrame is overwritten at the next tick.",
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
        Name = "WeldConstraint",
        Extends = "Instance",
        Module = "scene",
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
        },
    },
    {
        Name = "PointLight",
        Extends = "Instance",
        Module = "render",
        Storage = "PointLight",
        Doc = "A light radiating equally in every direction from its parent's position. Parent it to a BasePart or "
            .. "an Attachment; a light with no such ancestor lights nothing.",
        Properties = {
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
                Name = "Shadows",
                Inert = true,
                Type = "boolean",
                Doc = "Whether this light should cast shadows. Stored and reported faithfully, and not yet acted "
                    .. "on: this release casts shadows from the sun alone.",
            },
        },
    },
    {
        Name = "SpotLight",
        Extends = "Instance",
        Module = "render",
        Storage = "SpotLight",
        Doc = "A light confined to a cone about its parent's forward direction. A sibling of PointLight rather than "
            .. "a subclass of it: the two share four properties and no behaviour.",
        Properties = {
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
                Name = "Shadows",
                Inert = true,
                Type = "boolean",
                Doc = "Whether this light should cast shadows. Stored and not yet acted on, as on PointLight.",
            },
        },
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
                    .. "that is the safe one: an action nobody thought about fires where R10 holds and where the "
                    .. "input replay can see it. `Render` is for camera look and for UI, and an action read from "
                    .. "a Render context is frame-rate-dependent by construction.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "InputAction",
        Extends = "Instance",
        Module = "input",
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
                    .. "replay gate a replay of input rather than of a bot.",
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
                    .. "widget in v1 and there is not meant to be one -- a rebinding screen is a game''s "
                    .. "screen.",
            },
            {
                Name = "Image",
                Type = "Content",
                Default = '""',
                Doc = "A glyph for this binding, as an `asset://` URI. Empty means none.",
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
        Storage = "ScreenGui",
        Doc = "The root of one screen-space UI tree, parented to `UIService` (§2.2). Everything under it is "
            .. "laid out against the window and drawn over the world.\n\nIt is the unit of layout: a write "
            .. "that changes a layout marks the nearest ScreenGui dirty, and a screen nothing changed does not "
            .. "run the solver at all. That is a design constraint rather than an optimisation -- the "
            .. "milestone's benchmark asserts ZERO solver invocations on an idle frame, because \"about zero "
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
        Name = "UIObject",
        Extends = "Instance",
        Module = "ui",
        Tags = { "Abstract", "NotCreatable" } :: { Schema.ClassTag },
        Storage = "UIObject",
        Doc = "Anything that occupies a rectangle on screen (§2.2). Every property here is one the layout "
            .. "solver or the 2D pass reads, which is what makes it the base rather than a convenience.\n\n"
            .. "Coordinates are `UDim2`: a fraction of the parent plus a pixel offset, on each axis. That "
            .. "pair is what lets one layout be correct at every resolution, and it is why the goldens this "
            .. "milestone records are taken at two.",
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
                    .. "spinning icon reflowing a list every frame.",
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
        Doc = "Text in a box (§2.2). No RichText in v1, and no complex-script shaping -- codepoints are laid "
            .. "out left to right with kerning, which ADR 0011 records as a flagged i18n gap with a HarfBuzz "
            .. "seam behind it.",
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
                Name = "HorizontalAlignment",
                Type = "Enum.HorizontalAlignment",
                Default = "Enum.HorizontalAlignment.Center",
                Doc = "Where the text sits across the box. One alignment vocabulary shared with the layouts "
                    .. "(§2.5, divergence #23).",
            },
            {
                Name = "VerticalAlignment",
                Type = "Enum.VerticalAlignment",
                Default = "Enum.VerticalAlignment.Center",
                Doc = "Where it sits down the box.",
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
        Doc = "A single-line editable field (§2.2). Typed text, backspace and a caret; no selection, no "
            .. "clipboard, no undo and no IME composition beyond what the platform delivers as text.",
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
                    .. '"the player went somewhere else".',
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
            .. "music track.\n\nNamed `AudioGroup` rather than `SoundGroup` for the reason §2.5's divergence 19 "
            .. "gives: the service is `AudioService`, and one prefix across a family beats two.",
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
            .. "the instance sits rather than a flag that could disagree with it.\n\n**Its timeline is the "
            .. "SimClock's, not the mixer's** (M6 brief, Decision 9). `TimePosition` advances by "
            .. "`FixedTimestep * PlaybackSpeed` per tick and `Ended` fires from that timeline, so a replay "
            .. "reproduces both exactly and a headless run with no audio device produces the same `Ended` on "
            .. "the same tick as a run with speakers. What the speakers do is downstream of the simulation and "
            .. "never an input to it -- otherwise a script reading `TimePosition` would be reading the wall "
            .. "clock through a side door (R10).",
        Properties = {
            {
                Name = "Content",
                Type = "Content",
                Default = '""',
                Doc = "The audio, as an `asset://` URI. WAV, MP3, FLAC and Ogg Vorbis, decoded once and "
                    .. "cached: a sound does not re-read its file sixty times a second.\n\nA URI that "
                    .. "names nothing still PLAYS, as a generated tone whose pitch comes from a hash of the "
                    .. "id. A sound that went silent because a file was missing is a bug report about the "
                    .. "sound; a placeholder that is audibly a placeholder is one about the file. "
                    .. '`DebugService:GetStat("AudioClipsMissing")` is how a script asks which it '
                    .. "got.\n\nWhere in the file the sound is comes from `TimePosition` every frame "
                    .. "rather than from a cursor the mixer keeps, so the audio is a function of the "
                    .. "simulation and a replay reproduces it (M6 brief, Decision 9).",
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
                Doc = "Where the timeline is, in seconds. Writable, which is how a script seeks; the write "
                    .. "lands on the next tick like every other, and past the end it stops or wraps exactly as "
                    .. "arriving there would.",
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
                Doc = "Starts it from `TimePosition`, which is 0 for a sound that has never played and "
                    .. "wherever `Stop` left it otherwise. Playing one that is already playing is a no-op "
                    .. "rather than a restart -- restarting is `TimePosition = 0` and then this.",
            },
            {
                Name = "Pause",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Stops the timeline where it is. `Play` resumes from there.",
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
                Doc = "Fired on the tick the timeline reaches the end of a sound that is not `Looped`. From "
                    .. "the SIM timeline rather than from the mixer, so it lands on the same tick in a replay, "
                    .. "in a headless run, and on a machine whose audio buffer is four times the size.",
            },
            {
                Name = "Loaded",
                Parameters = {},
                Doc = "Fired when this sound's `Content` has been decoded and is ready to play. **Fires "
                    .. "immediately in v1**, because a sound has nothing to load until M7's asset pipeline -- "
                    .. "declared now so that code written today does not have to change when it does.",
            },
        } :: { Schema.Event },
    },
    {
        Name = "AnimationPlayer",
        Extends = "Instance",
        Module = "render",
        Doc = "Plays a skinned mesh's animation clips (§2.2). **Parent it to the `MeshPart` whose skeleton the "
            .. "clips belong to** -- that is where it looks for them, and an `AnimationPlayer` parented "
            .. "anywhere else finds nothing rather than guessing.\n\nIt stores nothing: the tracks are the "
            .. "state, and each one is a handle a script holds rather than a child in the tree. Sampling "
            .. "happens at `PreAnimation` on the SimClock, so a clip's position at a given tick is the same "
            .. "in a replay as it was live.\n\nv1 is clip playback and linear blending -- no state machines, "
            .. "no IK, no root motion (roadmap M6).",
        Methods = {
            {
                Name = "LoadAnimation",
                Parameters = { { Name = "content", Type = "Content" } },
                Returns = { "AnimationTrack" },
                ThreadSafety = "Unsafe",
                Doc = "A track for one clip. Everything after a `#` is the clip's NAME inside the file, and a "
                    .. "string with no `#` is a clip name in this player's own mesh -- so the common case is "
                    .. '`player:LoadAnimation("Walk")`.\n\nA path before the `#` must be the mesh this player '
                    .. "is under. v1 has no asset pipeline (M7 is where a clip becomes addressable on its "
                    .. "own), so a clip exists only inside the file its skeleton came from, and a URN naming "
                    .. "a different file loads nothing rather than playing the wrong rig.\n\n**Load a track "
                    .. "once and keep it.** It always returns a track, even for a clip that is not there -- a "
                    .. "mesh that has not finished loading would otherwise make an ordinary frame a nil index "
                    .. "-- and every call is a handle the VM holds until the world goes away.",
            },
        } :: { Schema.Method },
    },
}

local Definition: Schema.Definition = {
    Classes = Classes,
}

return Definition
