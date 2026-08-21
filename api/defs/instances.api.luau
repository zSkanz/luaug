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
                Doc = "Which primitive solid this part is.",
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
                Doc = "How closely the collision shape follows the rendered geometry. `Precise` is accepted and "
                    .. "collides as `Hull` in this release -- the property reads back what was written, because a "
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
                Doc = "Launches the character upward at JumpSpeed at the next simulation tick, if it is Grounded. "
                    .. "Ignored in mid-air rather than queued: a jump that fires the moment you land is a jump "
                    .. "you did not ask for.",
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
}

local Definition: Schema.Definition = {
    Classes = Classes,
}

return Definition
