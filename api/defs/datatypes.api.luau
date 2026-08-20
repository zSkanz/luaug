--!strict
--- Value types (api-design.md §2.3).
---
--- Constructors and namespace constants are camelCase; members reached through
--- a value are PascalCase (ADR 0034). The schema's validator enforces both, so
--- the split is not something a definition can get wrong quietly.
---
--- The M2 set only: `Vector3`, `CFrame`, `Color3`, `Random`, `Signal` and
--- `Connection`. `Vector2`, `UDim`, `UDim2`, `Rect`, `TweenInfo`,
--- `RaycastParams` and `RaycastResult` arrive with the milestone that gives
--- each one meaning; `Content` is an alias of `string` and is emitted by the
--- definition generator rather than declared here.
---
--- `Module` is `script` for every entry: a value type is bound in the VM by the
--- `script` module (m2-kickoff §"module map"), not registered into the scene
--- like an Instance class -- nothing here has an `InstanceId`.
---
--- `Constructors` holds every namespace-level *function*, not only the ones
--- that construct: `vector.magnitude` and `vector.clamp` sit beside
--- `Vector3.new` because they are reached the same way, and the casing rule
--- keys on how a name is reached rather than on what it does.

local Schema = require("../schema")

--- Annotated on its own rather than inline in `Definition`, because the new
--- solver infers an array literal's element type from the elements it can see
--- and then holds every later entry to it -- so the first datatype that omits
--- an optional field makes every other one an error. The annotation is what
--- lets `Schema.Datatype` flow down into the literal instead.
---
--- The same thing happens one level down, and for a specific reason: an
--- expected type does not propagate through an *optional* field, so
--- `Constructors: { Method }?` leaves its literal to infer itself while
--- `Parameters: { Parameter }` inherits fine. The `:: { Schema.Method }` and
--- `:: { Schema.Property }` assertions on the four member lists supply what the
--- `?` swallows. They are not casts that hide anything -- a misspelt field or a
--- wrong value type still fails exactly where it is written; verified against
--- both a typo and a bad type before being relied on.
local Datatypes: { Schema.Datatype } = {
    {
        Name = "Vector3",
        Module = "script",
        Doc = "The 3-component f32 vector, which IS the Luau VM's native `vector` primitive rather than a "
            .. 'userdata wrapping one (ADR 0013): `typeof` answers `"vector"`, arithmetic allocates nothing, '
            .. "and the components are the primitive's own lowercase `x`/`y`/`z` -- the single documented "
            .. "exception to the casing rules (§9). The `Vector3` global is the `vector` library plus `new`, so "
            .. "every function below is reachable under both names and `Vector3.zero == vector.zero`; it is a "
            .. "superset table and no table identity is promised. Operators are the primitive's own and all of "
            .. "them: `+`, `-` (binary and unary), `*` and `/` with a scalar on either side or component-wise "
            .. "with another vector, `//`, `%`, and `==`. Precision is millimetre-exact to roughly ±8 km and "
            .. "degrades to ~16 mm at ±131 km where the exponent range ends, which is why open-world math goes "
            .. "through CFrame's f64 translation and not through a position (ADR 0013 addendum, ADR 0014).",
        Constructors = {
            {
                Name = "new",
                Parameters = {
                    { Name = "x", Type = "number?", Default = "0" },
                    { Name = "y", Type = "number?", Default = "0" },
                    { Name = "z", Type = "number?", Default = "0" },
                },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Constructs a vector; a missing component is 0. Bound as the `vectorCtor` compile option "
                    .. "so a literal call is constant-folded and a dynamic one is a fastcall -- neither "
                    .. "allocates.",
            },
            {
                Name = "create",
                Parameters = {
                    { Name = "x", Type = "number" },
                    { Name = "y", Type = "number" },
                    { Name = "z", Type = "number?", Default = "0" },
                },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "The official vector stdlib's spelling of the constructor, carried so that code written "
                    .. "against another Luau runtime ports unchanged. Unlike `new` it requires `x` and `y`, "
                    .. "because "
                    .. "the stdlib checks argument count rather than accepting `nil` as zero.",
            },
            {
                Name = "magnitude",
                Parameters = { { Name = "v", Type = "vector" } },
                Returns = { "number" },
                ThreadSafety = "Safe",
                Doc = "The vector's length. Preferred over the `.Magnitude` field, which reaches the same "
                    .. "number through the metatable and is measurably slower.",
            },
            {
                Name = "normalize",
                Parameters = { { Name = "v", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "The vector scaled to unit length. A zero vector has no direction to preserve and "
                    .. "normalizing one yields NaN components rather than an error.",
            },
            {
                Name = "dot",
                Parameters = { { Name = "a", Type = "vector" }, { Name = "b", Type = "vector" } },
                Returns = { "number" },
                ThreadSafety = "Safe",
                Doc = "The dot product of two vectors.",
            },
            {
                Name = "cross",
                Parameters = { { Name = "a", Type = "vector" }, { Name = "b", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "The cross product, right-handed like the world it describes: "
                    .. "`cross(right, up) == -look` for any frame's basis (§2.3, handedness).",
            },
            {
                Name = "angle",
                Parameters = {
                    { Name = "a", Type = "vector" },
                    { Name = "b", Type = "vector" },
                    { Name = "axis", Type = "vector?", Default = "nil" },
                },
                Returns = { "number" },
                ThreadSafety = "Safe",
                Doc = "The angle between two vectors in radians, in [0, π]. With `axis`, the result is signed "
                    .. "about it by the right-hand rule and so spans (-π, π] -- which is the only way to tell "
                    .. "a rotation from its mirror image.",
            },
            {
                Name = "lerp",
                Parameters = {
                    { Name = "a", Type = "vector" },
                    { Name = "b", Type = "vector" },
                    { Name = "alpha", Type = "number" },
                },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Component-wise linear interpolation from `a` to `b`. `alpha` is not clamped, so values "
                    .. "outside [0, 1] extrapolate.",
            },
            {
                Name = "floor",
                Parameters = { { Name = "v", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Each component rounded toward negative infinity.",
            },
            {
                Name = "ceil",
                Parameters = { { Name = "v", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Each component rounded toward positive infinity.",
            },
            {
                Name = "abs",
                Parameters = { { Name = "v", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Each component's absolute value.",
            },
            {
                Name = "sign",
                Parameters = { { Name = "v", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Each component reduced to -1, 0 or 1 by its sign.",
            },
            {
                Name = "clamp",
                Parameters = {
                    { Name = "v", Type = "vector" },
                    { Name = "min", Type = "vector" },
                    { Name = "max", Type = "vector" },
                },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Each component clamped into the box `min`..`max`. An inverted box is an error rather "
                    .. "than an empty result, because there is no component value that satisfies it.",
            },
            {
                Name = "min",
                Parameters = { { Name = "a", Type = "vector" }, { Name = "rest", Type = "...vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "The component-wise minimum of every argument -- a corner of the bounding box, not "
                    .. "whichever argument is shortest.",
            },
            {
                Name = "max",
                Parameters = { { Name = "a", Type = "vector" }, { Name = "rest", Type = "...vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "The component-wise maximum of every argument -- the opposite corner of the bounding box "
                    .. "`min` returns.",
            },
        } :: { Schema.Method },
        Constants = {
            {
                Name = "zero",
                Type = "vector",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "`(0, 0, 0)`. Reachable as both `vector.zero` and `Vector3.zero`, and equal either way.",
            },
            {
                Name = "one",
                Type = "vector",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "`(1, 1, 1)`, the identity for component-wise multiplication.",
            },
        } :: { Schema.Property },
        Properties = {
            {
                Name = "x",
                Type = "number",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The X component. Lowercase because it is a field of the VM primitive itself, like a "
                    .. "string's length, and not a member of a type this project defines -- the one documented "
                    .. "exception to the PascalCase rule (§9, divergence #9). `v.X` reads the same component "
                    .. "rather than raising, because the interpreter answers a single-character index on a "
                    .. "vector case-insensitively before any metatable is consulted; lowercase is still the "
                    .. "only spelling declared, so the uppercase one is a type error.",
            },
            {
                Name = "y",
                Type = "number",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The Y component, up in the engine's Y-up world. Lowercase for the same reason as `x`.",
            },
            {
                Name = "z",
                Type = "number",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The Z component. Forward is -Z, so a forward-facing direction has a negative `z`. "
                    .. "Lowercase for the same reason as `x`.",
            },
            {
                Name = "Magnitude",
                Type = "number",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The vector's length, reached through the primitive's metatable. It is a convenience for "
                    .. "readability at the cost of speed: `vector.magnitude(v)` is a fastcall and this is not.",
            },
            {
                Name = "Unit",
                Type = "vector",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The vector scaled to length 1, reached through the metatable. Prefer "
                    .. "`vector.normalize(v)` in hot code for the same reason as `Magnitude`.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "Dot",
                Parameters = { { Name = "other", Type = "vector" } },
                Returns = { "number" },
                ThreadSafety = "Safe",
                Doc = "The dot product with `other`; the method form of `vector.dot`.",
            },
            {
                Name = "Cross",
                Parameters = { { Name = "other", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "The cross product with `other`, in that order; the method form of `vector.cross`. The "
                    .. "operation does not commute, so `a:Cross(b) == -b:Cross(a)`.",
            },
            {
                Name = "Lerp",
                Parameters = { { Name = "goal", Type = "vector" }, { Name = "alpha", Type = "number" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Linear interpolation toward `goal`; the method form of `vector.lerp`. `alpha` is not "
                    .. "clamped.",
            },
            {
                Name = "Angle",
                Parameters = {
                    { Name = "other", Type = "vector" },
                    { Name = "axis", Type = "vector?", Default = "nil" },
                },
                Returns = { "number" },
                ThreadSafety = "Safe",
                Doc = "The angle to `other` in radians; the method form of `vector.angle`, `axis` included, so "
                    .. "the signed form is available without switching spelling.",
            },
        } :: { Schema.Method },
    },
    {
        Name = "CFrame",
        Module = "script",
        Doc = "A rigid transform: an orthonormal rotation basis plus a translation. The translation is **f64** "
            .. "and is the world-precision source of truth the whole engine is built around (ADR 0014) -- "
            .. "`Position` is its f32 rounding, so gameplay math past a few kilometres from the origin must "
            .. "stay in CFrame rather than round-tripping through a vector. The world is right-handed, Y-up, "
            .. "-Z forward, matching glTF, and every sign in this type follows from that. Operators: "
            .. "`CFrame * CFrame` composes, `CFrame * vector` transforms a **point** (the translation is "
            .. "applied), and `==` is exact and component-wise -- an identity test, never a tolerance.",
        Constructors = {
            {
                Name = "new",
                Parameters = {
                    { Name = "x", Type = "number?", Default = "0" },
                    { Name = "y", Type = "number?", Default = "0" },
                    { Name = "z", Type = "number?", Default = "0" },
                },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "A frame with the identity rotation at the given position; a missing component is 0, so "
                    .. "`CFrame.new()` is the identity. The components are taken as f64 and stored as f64.",
            },
            {
                Name = "new",
                Parameters = { { Name = "position", Type = "vector" } },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "A frame with the identity rotation at `position`. The overload exists so a vector does "
                    .. "not have to be unpacked to be placed, but it enters through f32 -- construct from "
                    .. "numbers where the extra precision matters.",
            },
            {
                Name = "lookAt",
                Parameters = {
                    { Name = "position", Type = "vector" },
                    { Name = "target", Type = "vector" },
                    { Name = "up", Type = "vector?", Default = "Vector3.new(0, 1, 0)" },
                },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "A frame at `position` whose `LookVector` points at `target`, with `up` biasing the roll "
                    .. "and defaulting to `(0, 1, 0)`. Where the direction is degenerate or `up` is parallel "
                    .. "to it the result is the identity rotation at `position`, not a frame full of NaN: a "
                    .. "camera pointed at itself should stop moving, not poison every value it touches.",
            },
            {
                Name = "fromEuler",
                Parameters = {
                    { Name = "rx", Type = "number" },
                    { Name = "ry", Type = "number" },
                    { Name = "rz", Type = "number" },
                    { Name = "order", Type = "Enum.RotationOrder?", Default = "Enum.RotationOrder.YXZ" },
                },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "The ONE euler constructor -- there is no `fromEulerAnglesXYZ`/`YXZ` family, because the "
                    .. "order belongs in an argument and not in a name. Angles are radians, applied as "
                    .. "**intrinsic** rotations in `order` (each about the axes the previous one produced) and "
                    .. "signed by the right-hand rule about each axis.",
            },
            {
                Name = "fromAxisAngle",
                Parameters = { { Name = "axis", Type = "vector" }, { Name = "angle", Type = "number" } },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "A rotation of `angle` radians about `axis` by the right-hand rule, at the origin. The "
                    .. "axis is normalized on the way in, so its length carries no meaning.",
            },
            {
                Name = "fromQuaternion",
                Parameters = {
                    { Name = "position", Type = "vector" },
                    { Name = "qx", Type = "number" },
                    { Name = "qy", Type = "number" },
                    { Name = "qz", Type = "number" },
                    { Name = "qw", Type = "number" },
                },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "A frame at `position` rotated by the quaternion `(qx, qy, qz, qw)` -- w last, matching "
                    .. "`ToQuaternion`'s tail, which is the half of the convention people get wrong. The "
                    .. "quaternion is normalized on the way in.",
            },
            {
                Name = "fromMatrix",
                Parameters = {
                    { Name = "position", Type = "vector" },
                    { Name = "right", Type = "vector" },
                    { Name = "up", Type = "vector" },
                    { Name = "back", Type = "vector" },
                },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "A frame built from an explicit basis. The third axis is **back**, not forward, so "
                    .. "`LookVector == -back`. Axes that are neither unit length nor mutually perpendicular "
                    .. "are stored as given: a constructor that silently repaired its input would hide the bug "
                    .. "that produced it. `Orthonormalize` is the explicit repair.",
            },
        } :: { Schema.Method },
        Constants = {
            {
                Name = "identity",
                Type = "CFrame",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The identity transform: no rotation, at the origin. `cf * CFrame.identity == cf`.",
            },
        } :: { Schema.Property },
        Properties = {
            {
                Name = "Position",
                Type = "vector",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The translation, rounded from f64 to f32 at every magnitude -- reading it is lossy far "
                    .. "from the origin and reading it twice is not (the rounding is a function, not a drift). "
                    .. "The f64 value never leaves the CFrame, which is what makes composing transforms exact "
                    .. "where composing positions is not.",
            },
            {
                Name = "Rotation",
                Type = "CFrame",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The same rotation basis with a zero translation, so a pure translation's `Rotation` is "
                    .. "the identity. This is what the `VectorTo*Space` pair is defined in terms of.",
            },
            {
                Name = "RightVector",
                Type = "vector",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The basis's +X column.",
            },
            {
                Name = "UpVector",
                Type = "vector",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The basis's +Y column.",
            },
            {
                Name = "LookVector",
                Type = "vector",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The basis's **-Z** column -- forward is -Z in a right-handed Y-up world, which is why "
                    .. "`RightVector:Cross(UpVector) == -LookVector` rather than `LookVector`.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "Inverse",
                Parameters = {},
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "The transform that undoes this one: `cf * cf:Inverse() == CFrame.identity` up to "
                    .. "floating-point rounding.",
            },
            {
                Name = "Lerp",
                Parameters = { { Name = "goal", Type = "CFrame" }, { Name = "alpha", Type = "number" } },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "Interpolates the translation linearly and **slerps** the rotation, so the halfway frame "
                    .. "sits at an equal angle from each end rather than cutting the corner. `alpha` is not "
                    .. "clamped and values outside [0, 1] extrapolate.",
            },
            {
                Name = "Orthonormalize",
                Parameters = {},
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "Repairs a drifted or hand-built basis, preserving the translation and treating "
                    .. "`LookVector` as authoritative: the look direction is normalized, `UpVector` is made "
                    .. "perpendicular to it and normalized, and `RightVector` is whatever their cross product "
                    .. "requires. This is the explicit counterpart to `fromMatrix` storing what it is given.",
            },
            {
                Name = "ToWorldSpace",
                Parameters = { { Name = "cf", Type = "CFrame" } },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "Reads `cf` as being expressed relative to this frame and returns it in world space. "
                    .. "Defined as `self * cf`.",
            },
            {
                Name = "ToObjectSpace",
                Parameters = { { Name = "cf", Type = "CFrame" } },
                Returns = { "CFrame" },
                ThreadSafety = "Safe",
                Doc = "Expresses the world-space frame `cf` relative to this one. Defined as "
                    .. "`self:Inverse() * cf`.",
            },
            {
                Name = "PointToWorldSpace",
                Parameters = { { Name = "point", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Moves a **position** out of this frame into world space, translation included. Defined "
                    .. "as `self * point`.",
            },
            {
                Name = "PointToObjectSpace",
                Parameters = { { Name = "point", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Moves a world-space **position** into this frame, translation included. Defined as "
                    .. "`self:Inverse() * point`.",
            },
            {
                Name = "VectorToWorldSpace",
                Parameters = { { Name = "direction", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Moves a **direction** out of this frame into world space, rotation only. Defined as "
                    .. "`self.Rotation * direction`. A surface normal run through `PointToWorldSpace` instead "
                    .. "comes out wrong by exactly the frame's position.",
            },
            {
                Name = "VectorToObjectSpace",
                Parameters = { { Name = "direction", Type = "vector" } },
                Returns = { "vector" },
                ThreadSafety = "Safe",
                Doc = "Moves a world-space **direction** into this frame, rotation only. Defined as "
                    .. "`self.Rotation:Inverse() * direction`.",
            },
            {
                Name = "ToEuler",
                Parameters = {
                    { Name = "order", Type = "Enum.RotationOrder?", Default = "Enum.RotationOrder.YXZ" },
                },
                Returns = { "number", "number", "number" },
                ThreadSafety = "Safe",
                Doc = "Decomposes the rotation into `(rx, ry, rz)` radians in `fromEuler`'s parameter order, "
                    .. "and is its inverse for the same `order`. Euler angles are not unique, so a round trip "
                    .. "returns an equivalent rotation rather than necessarily the same three numbers.",
            },
            {
                Name = "ToAxisAngle",
                Parameters = {},
                Returns = { "vector", "number" },
                ThreadSafety = "Safe",
                Doc = "Decomposes the rotation into `(axis, angle)` with a unit axis and the angle in radians; "
                    .. "the inverse of `fromAxisAngle`.",
            },
            {
                Name = "ToQuaternion",
                Parameters = {},
                Returns = { "number", "number", "number", "number" },
                ThreadSafety = "Safe",
                Doc = "Decomposes the rotation into `(qx, qy, qz, qw)` -- w last, matching "
                    .. "`fromQuaternion`'s tail, whose inverse this is.",
            },
        } :: { Schema.Method },
        Operators = {
            {
                Name = "__mul",
                Parameters = { { Name = "other", Type = "CFrame" } },
                Returns = { "CFrame" },
                Doc = 'Composes two transforms: `a * b` reads as "first `b`, then `a`", the column-vector '
                    .. "convention every matrix in the engine follows.",
            },
            {
                Name = "__mul",
                Parameters = { { Name = "point", Type = "vector" } },
                Returns = { "vector" },
                Doc = "Transforms a **point** -- the translation is applied. `VectorToWorldSpace` is the "
                    .. "direction form, and a surface normal put through this one comes out wrong by exactly "
                    .. "the frame's position.",
            },
            {
                Name = "__eq",
                Parameters = { { Name = "other", Type = "CFrame" } },
                Returns = { "boolean" },
                Doc = "Exact and component-wise: an identity test, never a tolerance. Two frames built by "
                    .. "different routes to the same place are equal only if every stored number matches.",
            },
        } :: { Schema.Operator },
    },
    {
        Name = "Color3",
        Module = "script",
        Doc = "An RGB colour in three f32 channels, nominally 0-1 and deliberately **not clamped**: values "
            .. "outside the range are legal and meaningful for HDR emissives and tint multipliers over 1, so "
            .. "clamping belongs to the consumer that actually has a range to respect. No BrickColor. `==` is "
            .. "exact and component-wise.",
        Constructors = {
            {
                Name = "new",
                Parameters = {
                    { Name = "r", Type = "number?", Default = "0" },
                    { Name = "g", Type = "number?", Default = "0" },
                    { Name = "b", Type = "number?", Default = "0" },
                },
                Returns = { "Color3" },
                ThreadSafety = "Safe",
                Doc = "A colour from three 0-1 channels, each defaulting to 0, so `Color3.new()` is black. "
                    .. "Out-of-range values are kept as given.",
            },
            {
                Name = "fromRGB",
                Parameters = {
                    { Name = "r", Type = "number" },
                    { Name = "g", Type = "number" },
                    { Name = "b", Type = "number" },
                },
                Returns = { "Color3" },
                ThreadSafety = "Safe",
                Doc = "A colour from three 0-255 channels, which is the range colour pickers and design tools "
                    .. "speak in. The channels are divided by 255 and stored, not rounded to a byte.",
            },
            {
                Name = "fromHSV",
                Parameters = {
                    { Name = "h", Type = "number" },
                    { Name = "s", Type = "number" },
                    { Name = "v", Type = "number" },
                },
                Returns = { "Color3" },
                ThreadSafety = "Safe",
                Doc = "A colour from hue, saturation and value -- **all three** 0-1, hue included, so "
                    .. "`fromHSV(1/3, 1, 1)` is green and not a hue of one third of a degree.",
            },
            {
                Name = "fromHex",
                Parameters = { { Name = "hex", Type = "string" } },
                Returns = { "Color3" },
                ThreadSafety = "Safe",
                Doc = "A colour from a hex string, accepting `#rrggbb`, `rrggbb`, `#rgb` and `rgb` "
                    .. "case-insensitively, so the round trip with `ToHex` is defined in both directions and "
                    .. "neither call has to guess what the other meant. Anything else raises "
                    .. "`script.err.color_hex_invalid` rather than returning a silently wrong colour.",
            },
        } :: { Schema.Method },
        Properties = {
            {
                Name = "R",
                Type = "number",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The red channel, nominally 0-1 and not clamped. `c.r` raises "
                    .. "`script.err.unknown_member`: there is one spelling.",
            },
            {
                Name = "G",
                Type = "number",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The green channel, nominally 0-1 and not clamped.",
            },
            {
                Name = "B",
                Type = "number",
                ReadOnly = true,
                ThreadSafety = "Safe",
                Doc = "The blue channel, nominally 0-1 and not clamped.",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "Lerp",
                Parameters = { { Name = "goal", Type = "Color3" }, { Name = "alpha", Type = "number" } },
                Returns = { "Color3" },
                ThreadSafety = "Safe",
                Doc = "Channel-wise linear interpolation toward `goal` in RGB, which is the cheap blend and "
                    .. "not the perceptually even one. `alpha` is not clamped.",
            },
            {
                Name = "ToHSV",
                Parameters = {},
                Returns = { "number", "number", "number" },
                ThreadSafety = "Safe",
                Doc = "Decomposes the colour into `(h, s, v)`, all three 0-1; the inverse of `fromHSV`. Hue is "
                    .. "undefined for a grey and reported as 0.",
            },
            {
                Name = "ToHex",
                Parameters = {},
                Returns = { "string" },
                ThreadSafety = "Safe",
                Doc = 'Six lowercase hex digits with no leading `#` (`"ff8800"`). The channels are clamped '
                    .. "into 0-255 on the way out because eight bits cannot represent anything else -- this is "
                    .. "the one place a Color3 is narrowed, and it is why the round trip through `fromHex` "
                    .. "does not preserve an HDR value.",
            },
        } :: { Schema.Method },
        Operators = {
            {
                Name = "__eq",
                Parameters = { { Name = "other", Type = "Color3" } },
                Returns = { "boolean" },
                Doc = "Exact and component-wise. Channels are not clamped, so two colours that would render "
                    .. "identically after clamping are still unequal if their stored values differ.",
            },
        } :: { Schema.Operator },
    },
    {
        Name = "Random",
        Module = "script",
        Doc = "A seeded pseudorandom generator, and the only sanctioned source of randomness in simulation "
            .. "code (R10). A seeded stream is reproducible: the same seed yields the same sequence for the "
            .. "same engine build on the same platform, which is the level-B guarantee recorded replays rest "
            .. "on -- replays store seeds, not draws (ADR 0025).",
        Constructors = {
            {
                Name = "new",
                Parameters = { { Name = "seed", Type = "number?", Default = "nil" } },
                Returns = { "Random" },
                ThreadSafety = "Safe",
                Doc = "A generator seeded with `seed`, truncated toward zero. **Omitting the seed draws one "
                    .. "from the host and is not legal in simulation code**: it is exactly the unseeded RNG "
                    .. "R10 forbids, and `luaug check` flags it. Use it for cosmetics that never reach the "
                    .. "world hash, or seed it from something the simulation already knows.",
            },
        } :: { Schema.Method },
        Methods = {
            {
                Name = "NextNumber",
                Parameters = {
                    { Name = "min", Type = "number?", Default = "0" },
                    { Name = "max", Type = "number?", Default = "1" },
                },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "The next number in [min, max), defaulting to [0, 1) -- half-open, like every other "
                    .. "range in the engine. `min > max` raises `script.err.random_range`.",
            },
            {
                Name = "NextInteger",
                Parameters = { { Name = "min", Type = "number" }, { Name = "max", Type = "number" } },
                Returns = { "number" },
                ThreadSafety = "Unsafe",
                Doc = "The next integer in [min, max], inclusive at **both** ends. This is the one place the "
                    .. "engine is not half-open, which is why it is spelled out rather than inferred from "
                    .. "`NextNumber`. `min > max`, or a non-integer bound, raises `script.err.random_range`.",
            },
            {
                Name = "NextUnitVector",
                Parameters = {},
                Returns = { "vector" },
                ThreadSafety = "Unsafe",
                Doc = "A direction drawn **uniformly over the sphere**, not merely one of unit length -- "
                    .. "normalizing three independent draws clusters at the cube's corners, and this does not.",
            },
            {
                Name = "Clone",
                Parameters = {},
                Returns = { "Random" },
                ThreadSafety = "ReadParallel",
                Doc = "A copy positioned at the same point in the stream, continuing the same sequence "
                    .. "independently of the original. This is how a subsystem takes a deterministic branch "
                    .. "without consuming draws the rest of the frame is counting on.",
            },
        } :: { Schema.Method },
    },
    {
        Name = "RaycastParams",
        Module = "script",
        Doc = "What a cast is allowed to hit. Every query on `Workspace` takes one, and omitting it means "
            .. "everything queryable. Constructed from a table so that a call site reads as the question it "
            .. "is asking rather than as four positional arguments (§2.3).",
        Constructors = {
            {
                Name = "new",
                Parameters = { { Name = "options", Type = "{ Filter: { Instance }?, FilterType: Enum.RaycastFilterType?, CollisionGroup: string? }?", Default = "nil" } },
                Returns = { "RaycastParams" },
                ThreadSafety = "Safe",
                Doc = "Filter defaults to empty, FilterType to Exclude and CollisionGroup to the empty string, "
                    .. "which means every group. An empty Exclude filter therefore hits everything, and an "
                    .. "empty Include filter hits nothing -- which is what each word means and is worth "
                    .. "knowing before a query silently returns nil.",
            },
        } :: { Schema.Method },
        Properties = {
            {
                Name = "Filter",
                Type = "{ Instance }",
                ReadOnly = true,
                Doc = "The instances the filter names, as a fresh array on every read. Descendants of a named "
                    .. "instance are covered too, so filtering a Model filters its parts.",
            },
            {
                Name = "FilterType",
                Type = "Enum.RaycastFilterType",
                ReadOnly = true,
                Doc = "Whether Filter names what may be hit or what must be ignored.",
            },
            {
                Name = "CollisionGroup",
                Type = "string",
                ReadOnly = true,
                Doc = "The group a hit must be in, or the empty string for any group. A camera ray asking for "
                    .. "\"Terrain\" is cheaper than one that hits everything and filters afterwards.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "RaycastResult",
        Module = "script",
        Doc = "What a cast hit. Returned by `Workspace:Raycast` and `:Spherecast`, and nil when nothing was "
            .. "hit -- there is no \"empty result\" to check a field on, because a result that exists and "
            .. "means nothing is the shape of every nil-check somebody forgets.",
        Properties = {
            {
                Name = "Instance",
                Type = "BasePart",
                ReadOnly = true,
                Doc = "The part that was hit.",
            },
            {
                Name = "Position",
                Type = "vector",
                ReadOnly = true,
                Doc = "Where on it, in world space.",
            },
            {
                Name = "Normal",
                Type = "vector",
                ReadOnly = true,
                Doc = "The surface normal there, pointing out of the surface.",
            },
            {
                Name = "Distance",
                Type = "number",
                ReadOnly = true,
                Doc = "How far along the cast the hit is, in metres. The direction passed to a cast is not "
                    .. "normalised -- its length is the range -- so this is a distance rather than a fraction.",
            },
        } :: { Schema.Property },
    },
    {
        Name = "Signal",
        Module = "script",
        Doc = "THE signal type, spelled `Signal<T...>` where the pack is what handlers receive and `Wait` "
            .. "returns -- never `RBXScriptSignal` (divergence #4). Delivery is **deferred only** (ADR 0015): "
            .. "a fire enqueues, and handlers run at the next drain in the order described in §3.1, with "
            .. "connection order guaranteed within one fire. Script-created signals replace BindableEvent and "
            .. "BindableFunction outright (divergence #5). Handler errors are contained: each runs on its own "
            .. "coroutine, and an error stops neither the other handlers, nor the drain, nor the firing "
            .. "script.",
        Constructors = {
            {
                Name = "new",
                Parameters = {},
                Returns = { "Signal<T...>" },
                ThreadSafety = "Safe",
                Doc = "A script-owned signal. The pack is generic and inferred from the `Fire` and `Connect` "
                    .. "sites; annotate it (`Signal<string>`, `Signal<()>`) where inference has nothing to "
                    .. "work from, such as an array element type.",
            },
        } :: { Schema.Method },
        Methods = {
            {
                Name = "Connect",
                Parameters = { { Name = "handler", Type = "(T...) -> ()" } },
                Returns = { "Connection" },
                ThreadSafety = "Unsafe",
                Doc = "Registers `handler` and returns the Connection that ends it. Handlers of one fire run "
                    .. "in the order they were connected; ordering *between* different signals is queue order "
                    .. "and relying on it is a bug. `ConnectParallel` is a reserved name, absent in v1.",
            },
            {
                Name = "Once",
                Parameters = { { Name = "handler", Type = "(T...) -> ()" } },
                Returns = { "Connection" },
                ThreadSafety = "Unsafe",
                Doc = "Registers `handler` for a single invocation, disconnecting as it is invoked -- so a "
                    .. "signal fired twice before a drain still calls it exactly once. A handler that errors "
                    .. "has still been consumed.",
            },
            {
                Name = "Wait",
                Parameters = {},
                Returns = { "T..." },
                Yields = true,
                ThreadSafety = "Unsafe",
                Doc = "Parks the calling coroutine as a one-shot registration made at the moment of the call, "
                    .. "resumed by the next fire it is eligible for and returning that fire's arguments. It "
                    .. "takes its place among the connections by registration order, so a Wait registered "
                    .. "between handlers A and B resumes between them.",
            },
            {
                Name = "Fire",
                Parameters = { { Name = "values", Type = "T..." } },
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Enqueues an invocation of every current handler. Nothing runs before the next drain. "
                    .. "Arguments are captured by reference and not copied: mutating a table between the fire "
                    .. "and the drain is visible to the handlers.",
            },
            {
                Name = "Destroy",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Closes the signal. Already-queued fires find no live connections and invoke nothing, "
                    .. "and every Connection reports `Connected == false` once the drain ends. Only "
                    .. "script-created signals are destroyable; an instance's own signals are closed by "
                    .. "`Instance:Destroy`.",
            },
        } :: { Schema.Method },
    },
    {
        Name = "Connection",
        Module = "script",
        Doc = "The handle a `Signal:Connect` or `:Once` returns -- never `RBXScriptConnection` (divergence "
            .. "#4). Its only job is to end the connection it represents; it is not constructible on its own.",
        Properties = {
            {
                Name = "Connected",
                Type = "boolean",
                ReadOnly = true,
                Transient = true,
                ThreadSafety = "ReadParallel",
                Doc = "Whether the connection is still live. False after `Disconnect`, after the owning signal "
                    .. "is destroyed, and after a `:Once` handler has run. No `Is` prefix: boolean properties "
                    .. "do not carry one, boolean methods do (§9).",
            },
        } :: { Schema.Property },
        Methods = {
            {
                Name = "Disconnect",
                Parameters = {},
                Returns = {},
                ThreadSafety = "Unsafe",
                Doc = "Ends the connection. **Idempotent**: a second call is a no-op and `Connected` stays "
                    .. "false, so cleanup code never has to guard it. A fire already on the queue captured its "
                    .. "connection list before this call, so it may still have invoked the handler.",
            },
        } :: { Schema.Method },
    },
}

local Definition: Schema.Definition = {
    Datatypes = Datatypes,
}

return Definition
