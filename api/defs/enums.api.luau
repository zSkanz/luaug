--!strict
--- Enums (api-design.md §2.3, the Enum row).
---
--- The enums the surface actually uses, milestone by milestone: an enum nobody
--- can pass anywhere is a name squatting on a meaning we have not designed yet.
--- M6 adds the four the Input Action System needs, and `KeyCode` is the largest
--- enum in the surface by an order of magnitude -- it is the whole of what a
--- binding may name, and its item NAMES are the `platform` device layer's
--- enumerator names on purpose, so that the two tables can be checked against
--- each other rather than kept in step by hand.
---
--- Item values are contract rather than bookkeeping: they are what
--- serialization writes, what an api-dump diff compares, and -- for `LogLevel`
--- -- what a severity comparison reads. Every enum below numbers its items
--- sequentially from 0 in declaration order, and declaration order is §2.3's
--- own listing order, so the document and this file can be diffed by eye.
--- Nothing is spaced out to leave room for later insertions: reserving value
--- space for an item nobody has designed is the same squatting the paragraph
--- above refuses, differing only in that it squats on a number instead of a
--- name. A level or a shape added later is a surface decision either way, and
--- it will be made with the whole list in front of it.

local Schema = require("../schema")

local Definition: Schema.Definition = {
    Enums = {
        {
            Name = "PartShape",
            Doc = "The solid a `Part` renders and collides as; `MeshPart` carries geometry instead (§2.2).",
            Items = {
                { Name = "Block", Value = 0 },
                { Name = "Ball", Value = 1 },
                { Name = "Cylinder", Value = 2 },
                {
                    Name = "Capsule",
                    Value = 3,
                    Doc = "A cylinder closed by a hemisphere at each end.",
                },
                {
                    Name = "Wedge",
                    Value = 4,
                    Doc = "A box with one face sloped away to an edge: a right-triangular prism.",
                },
            },
        },
        {
            Name = "CollisionFidelity",
            Doc = "How closely a `MeshPart`'s collision shape follows the geometry it renders (§2.2). The pairing "
                .. "is a trade: the closer the shape, the more the solver pays for every contact against it.",
            Items = {
                {
                    Name = "Default",
                    Value = 0,
                    Doc = "The engine chooses. In this release that is `Hull`, and the property reports `Default` "
                        .. "rather than what it resolved to, because the point of the item is that the choice is "
                        .. "the engine's to change.",
                },
                { Name = "Hull", Value = 1, Doc = "One convex hull around the whole mesh." },
                {
                    Name = "Box",
                    Value = 2,
                    Doc = "The mesh's bounding box, which is the cheapest shape there is and the least faithful.",
                },
                {
                    Name = "Precise",
                    Value = 3,
                    Doc = "The triangles themselves. Accepted and not yet implemented: this release collides "
                        .. "against a hull and says so through this property reading back `Precise` while "
                        .. "behaving as `Hull`. A triangle-mesh collider is asset-pipeline work (M7).",
                },
            },
        },
        {
            Name = "RaycastFilterType",
            Doc = "Whether a `RaycastParams` filter list names what a cast may hit or what it must ignore (§2.3).",
            Items = {
                { Name = "Exclude", Value = 0, Doc = "Everything except the listed instances and their descendants." },
                { Name = "Include", Value = 1, Doc = "Only the listed instances and their descendants." },
            },
        },
        {
            Name = "CharacterState",
            Doc = "Whether a `CharacterBody` is standing on something (§2.2). Two items and not three: ground too "
                .. "steep to walk on reads as `Airborne`, because the question a script asks this property is "
                .. "whether it may jump.",
            Items = {
                { Name = "Grounded", Value = 0 },
                { Name = "Airborne", Value = 1 },
            },
        },
        {
            Name = "RotationOrder",
            Doc = "The order `CFrame.fromEuler` applies its three angles, and `CFrame:ToEuler` recovers them: "
                .. "intrinsic rotations, each about the axes the previous one produced, read left to right. "
                .. "All six permutations are declared.",
            Items = {
                { Name = "XYZ", Value = 0 },
                { Name = "XZY", Value = 1 },
                {
                    Name = "YXZ",
                    Value = 2,
                    Doc = "Yaw about Y, then pitch about X, then roll about Z -- what an omitted `order` "
                        .. "parameter means, `ToEuler` included.",
                },
                { Name = "YZX", Value = 3 },
                { Name = "ZXY", Value = 4 },
                { Name = "ZYX", Value = 5 },
            },
        },
        {
            Name = "LogLevel",
            Doc = "The severity of a log line, ascending: `Value` orders the levels, so a handler filters with "
                .. "`level.Value >= Enum.LogLevel.Warning.Value`. `DebugService.MessageOut` carries one with "
                .. "every message (§2.1).",
            Items = {
                {
                    Name = "Trace",
                    Value = 0,
                    Doc = "Per-frame or per-call detail, silent in any ordinary run.",
                },
                {
                    Name = "Debug",
                    Value = 1,
                    Doc = "Diagnostic detail for whoever is working on the code that emits it.",
                },
                {
                    Name = "Info",
                    Value = 2,
                    Doc = "Ordinary progress; `print` reports at this level.",
                },
                {
                    Name = "Warning",
                    Value = 3,
                    Doc = "Something is wrong and the run continues; `warn` reports at this level.",
                },
                {
                    Name = "Error",
                    Value = 4,
                    Doc = "A failure the emitting code could not handle, a contained handler error among them (§3.1).",
                },
            },
        },
        {
            Name = "RunContext",
            Doc = "Where a `Script` would run once the client/server split exists. Declared with both items in "
                .. "v1 and read by nothing: a `Script` runs identically whatever its `RunContext` says. The "
                .. "items exist so that code written today cannot come to mean something else by them later "
                .. "(§2.1).",
            Items = {
                {
                    Name = "Client",
                    Value = 0,
                    Doc = "The machine a player sits at.",
                },
                {
                    Name = "Server",
                    Value = 1,
                    Doc = "The authoritative simulation host.",
                },
            },
        },
        {
            Name = "KeyCode",
            Doc = "Every physical input an `InputBinding` can name: keyboard keys by their US-layout legend, "
                .. "mouse buttons and analogue motion, and the standard gamepad layout's buttons, axes and "
                .. "sticks. One enum rather than three, because a binding names ONE input and the question a "
                .. 'binding asks is never "which device" -- that is `InputBinding.DeviceType`, which says which '
                .. "device a PROMPT should be drawn for.\n\nItems are analogue or digital, and mixing them is "
                .. 'the one mistake this enum makes easy: a Bool action bound to `LeftStickX` is asking "is '
                .. 'this axis pressed", which has no answer. The engine treats an analogue source on a Bool '
                .. "action as pressed past half deflection rather than refusing, because refusing at bind time "
                .. "would mean a rebinding UI could hand the player an unusable choice.",
            Items = {
                {
                    Name = "Unknown",
                    Value = 0,
                    Doc = "No input at all. What an `InputBinding` carries before anything is assigned to it, and what a "
                        .. "binding whose key the running platform does not have reads back as.",
                },
                { Name = "Escape", Value = 1 },
                { Name = "F1", Value = 2 },
                { Name = "F2", Value = 3 },
                { Name = "F3", Value = 4 },
                { Name = "F4", Value = 5 },
                { Name = "F5", Value = 6 },
                { Name = "F6", Value = 7 },
                { Name = "F7", Value = 8 },
                { Name = "F8", Value = 9 },
                { Name = "F9", Value = 10 },
                { Name = "F10", Value = 11 },
                { Name = "F11", Value = 12 },
                { Name = "F12", Value = 13 },
                { Name = "A", Value = 14 },
                { Name = "B", Value = 15 },
                { Name = "C", Value = 16 },
                { Name = "D", Value = 17 },
                { Name = "E", Value = 18 },
                { Name = "F", Value = 19 },
                { Name = "G", Value = 20 },
                { Name = "H", Value = 21 },
                { Name = "I", Value = 22 },
                { Name = "J", Value = 23 },
                { Name = "K", Value = 24 },
                { Name = "L", Value = 25 },
                { Name = "M", Value = 26 },
                { Name = "N", Value = 27 },
                { Name = "O", Value = 28 },
                { Name = "P", Value = 29 },
                { Name = "Q", Value = 30 },
                { Name = "R", Value = 31 },
                { Name = "S", Value = 32 },
                { Name = "T", Value = 33 },
                { Name = "U", Value = 34 },
                { Name = "V", Value = 35 },
                { Name = "W", Value = 36 },
                { Name = "X", Value = 37 },
                { Name = "Y", Value = 38 },
                { Name = "Z", Value = 39 },
                { Name = "Digit0", Value = 40 },
                { Name = "Digit1", Value = 41 },
                { Name = "Digit2", Value = 42 },
                { Name = "Digit3", Value = 43 },
                { Name = "Digit4", Value = 44 },
                { Name = "Digit5", Value = 45 },
                { Name = "Digit6", Value = 46 },
                { Name = "Digit7", Value = 47 },
                { Name = "Digit8", Value = 48 },
                { Name = "Digit9", Value = 49 },
                { Name = "Space", Value = 50 },
                { Name = "Return", Value = 51 },
                { Name = "Tab", Value = 52 },
                { Name = "Backspace", Value = 53 },
                { Name = "LeftShift", Value = 54 },
                { Name = "RightShift", Value = 55 },
                { Name = "LeftControl", Value = 56 },
                { Name = "RightControl", Value = 57 },
                { Name = "LeftAlt", Value = 58 },
                { Name = "RightAlt", Value = 59 },
                { Name = "Left", Value = 60 },
                { Name = "Right", Value = 61 },
                { Name = "Up", Value = 62 },
                { Name = "Down", Value = 63 },
                { Name = "MouseLeft", Value = 64 },
                { Name = "MouseMiddle", Value = 65 },
                { Name = "MouseRight", Value = 66 },
                { Name = "MouseX1", Value = 67 },
                { Name = "MouseX2", Value = 68 },
                {
                    Name = "MouseMovement",
                    Value = 69,
                    Doc = "The pointer's motion this tick, as a Vector2 in pixels. An analogue source: bind it to a "
                        .. "Direction2D action for a look control, never to a Bool one.",
                },
                {
                    Name = "MouseWheel",
                    Value = 70,
                    Doc = "The wheel's motion this tick, in notches, positive away from the user. Analogue, like "
                        .. "MouseMovement -- a Direction1D source rather than a button.",
                },
                {
                    Name = "ButtonSouth",
                    Value = 71,
                    Doc = "The bottom face button: A on an Xbox pad, B on a Nintendo one, Cross on a PlayStation one. Named "
                        .. "by POSITION because the legend moves with the hardware and a saved binding must not.",
                },
                {
                    Name = "ButtonEast",
                    Value = 72,
                    Doc = "The right face button.",
                },
                {
                    Name = "ButtonWest",
                    Value = 73,
                    Doc = "The left face button.",
                },
                {
                    Name = "ButtonNorth",
                    Value = 74,
                    Doc = "The top face button.",
                },
                { Name = "ButtonBack", Value = 75 },
                { Name = "ButtonGuide", Value = 76 },
                { Name = "ButtonStart", Value = 77 },
                {
                    Name = "ButtonLeftStick",
                    Value = 78,
                    Doc = "The left stick pressed IN. The stick's direction is LeftThumbstick.",
                },
                {
                    Name = "ButtonRightStick",
                    Value = 79,
                    Doc = "The right stick pressed IN. The stick's direction is RightThumbstick.",
                },
                { Name = "ButtonLeftShoulder", Value = 80 },
                { Name = "ButtonRightShoulder", Value = 81 },
                { Name = "DpadUp", Value = 82 },
                { Name = "DpadDown", Value = 83 },
                { Name = "DpadLeft", Value = 84 },
                { Name = "DpadRight", Value = 85 },
                { Name = "LeftStickX", Value = 86 },
                { Name = "LeftStickY", Value = 87 },
                { Name = "RightStickX", Value = 88 },
                { Name = "RightStickY", Value = 89 },
                {
                    Name = "LeftTrigger",
                    Value = 90,
                    Doc = "The left trigger, 0 released to 1 fully pulled. Analogue over the positive range only, which is "
                        .. "why a released trigger reads 0 rather than -1.",
                },
                {
                    Name = "RightTrigger",
                    Value = 91,
                    Doc = "The right trigger, 0 released to 1 fully pulled.",
                },
                {
                    Name = "LeftThumbstick",
                    Value = 92,
                    Doc = "The left stick as a Vector2, both axes at once -- the source a Direction2D action binds for "
                        .. "movement. The individual axes are LeftStickX and LeftStickY.",
                },
                {
                    Name = "RightThumbstick",
                    Value = 93,
                    Doc = "The right stick as a Vector2.",
                },
            } :: { Schema.EnumItem },
        },
        {
            Name = "InputActionType",
            Doc = "What an `InputAction` produces, and therefore what `GetState` returns (§2.4, ADR 0029). The "
                .. "type is the action's, not the binding's: one action of one type may be driven by a key, a "
                .. "stick and an on-screen button at once, and every one of them has to answer in the same "
                .. "currency.",
            Items = {
                {
                    Name = "Bool",
                    Value = 0,
                    Doc = "Pressed or not. `GetState` returns a boolean, and this is the only type that fires "
                        .. "`Pressed` and `Released`.",
                },
                {
                    Name = "Direction1D",
                    Value = 1,
                    Doc = "One signed axis. `GetState` returns a number, -1 to 1 from a pair of keys and the "
                        .. "analogue value from a trigger or a wheel.",
                },
                {
                    Name = "Direction2D",
                    Value = 2,
                    Doc = "Two signed axes. `GetState` returns a `Vector2`, from a stick, from pointer motion, or "
                        .. "from the four composite keys on a binding.",
                },
                {
                    Name = "Direction3D",
                    Value = 3,
                    Doc = "Three signed axes, returning a `vector`. **Declared and not driveable in v1**: no "
                        .. "binding in §2.4's list names three axes, and inventing one would be designing the "
                        .. "binding surface backwards from an enum item. `GetState` returns the zero vector. The "
                        .. "item exists because the enum is designed once and an item added later is the harder "
                        .. "break -- the same reasoning `Enum.RunContext` carries in §2.1.",
                },
                {
                    Name = "ViewportPosition",
                    Value = 4,
                    Doc = "Where the pointer IS, rather than how it moved. `GetState` returns a `Vector2` in "
                        .. "window pixels with the origin at the top left.",
                },
            } :: { Schema.EnumItem },
        },
        {
            Name = "InputDeviceType",
            Doc = "A family of input hardware. Used for two things and neither of them is dispatch: "
                .. "`InputService.LastInputDeviceType` says what the player touched most recently, so a HUD can "
                .. "switch its prompts, and `InputBinding.DeviceType` says which family a binding is the "
                .. "preferred one for, which is what `GetPreferredBinding` answers with.",
            Items = {
                { Name = "KeyboardMouse", Value = 0 },
                { Name = "Gamepad", Value = 1 },
                {
                    Name = "Touch",
                    Value = 2,
                    Doc = "Declared because the enum is designed once (ADR 0029). Nothing produces it in v1: "
                        .. "there is no touch input and no mobile port (R15).",
                },
            } :: { Schema.EnumItem },
        },
        {
            Name = "UserInputType",
            Doc = "What KIND of input an `InputObject` came from, on `InputService`'s raw events (§2.4, "
                .. "ADR 0041). It is coarser than `KeyCode` on purpose: a handler that only cares whether "
                .. "something was typed at all reads this and stops, and one that cares which key reads "
                .. "`KeyCode` beside it.\n\nThe mouse buttons are separate items rather than one `Mouse` "
                .. "item plus a `KeyCode`, because the overwhelmingly common test is `== MouseButton1` and "
                .. "spelling that as two comparisons would be worse for no gain.",
            Items = {
                {
                    Name = "None",
                    Value = 0,
                    Doc = "What a default-constructed `InputObject` carries. Nothing produces it: an event that "
                        .. "fired came from something.",
                },
                { Name = "Keyboard", Value = 1 },
                { Name = "MouseButton1", Value = 2, Doc = "The primary button -- left on a right-handed mouse." },
                { Name = "MouseButton2", Value = 3, Doc = "The secondary button." },
                { Name = "MouseButton3", Value = 4, Doc = "The middle button, usually the wheel pressed in." },
                {
                    Name = "MouseMovement",
                    Value = 5,
                    Doc = "The pointer moved. Fires `InputChanged` and never `InputBegan` or `InputEnded`: "
                        .. "motion has no beginning and no end.",
                },
                {
                    Name = "MouseWheel",
                    Value = 6,
                    Doc = "The wheel turned. `InputChanged`, with the amount in `Delta.z` -- one notch is 1.",
                },
                {
                    Name = "Gamepad",
                    Value = 7,
                    Doc = "A pad button or stick. `KeyCode` says which, and a stick's deflection arrives as "
                        .. "`InputChanged` with the axis value in `Position`.",
                },
                {
                    Name = "Touch",
                    Value = 8,
                    Doc = "Declared because the enum is designed once. Nothing produces it in v1: there is no "
                        .. "touch input and no mobile port (R15), the same clause "
                        .. "`Enum.InputDeviceType.Touch` carries.",
                },
            } :: { Schema.EnumItem },
        },
        {
            Name = "InputRate",
            Doc = "Which clock an `InputContext` is dispatched on (ADR 0039). It is a property of the context "
                .. "rather than of the action, and it is NOT derived from `Priority`: priority orders "
                .. "fallthrough, and overloading it would make a low-priority render-rate context "
                .. "inexpressible -- and would change an action's determinism class the day somebody re-tuned a "
                .. "number for an unrelated reason.",
            Items = {
                {
                    Name = "Simulation",
                    Value = 0,
                    Doc = "The sim tick, with the fixed timestep. The default, because it is the safe one: an "
                        .. "action nobody thought about fires where R10 holds and where the replay gate can see "
                        .. "it. Everything a game DECIDES with should be here.",
                },
                {
                    Name = "Render",
                    Value = 1,
                    Doc = "The render frame, with a variable dt. For camera look and for UI, where a tick of "
                        .. "latency is visible. A gameplay decision taken from a render-rate action is "
                        .. "frame-rate-dependent by construction, and it is not recorded by the input replay -- "
                        .. "a render frame is not a unit the replay has.",
                },
            } :: { Schema.EnumItem },
        },
        {
            Name = "EasingStyle",
            Doc = "The shape of a tween's progress curve (§2.1). The set is the one tutorials and design tools "
                .. "already speak, so a curve named in one transfers to the other; the formulas are the "
                .. "published ones and `tests/fixtures/easing.luau` pins them as literal numbers rather than as "
                .. "a re-derivation.",
            Items = {
                {
                    Name = "Linear",
                    Value = 0,
                    Doc = "No easing at all: alpha is the progress. The only style whose direction changes "
                        .. "nothing.",
                },
                { Name = "Sine", Value = 1 },
                { Name = "Quad", Value = 2 },
                { Name = "Cubic", Value = 3 },
                { Name = "Quart", Value = 4 },
                { Name = "Quint", Value = 5 },
                { Name = "Exponential", Value = 6 },
                { Name = "Circular", Value = 7 },
                {
                    Name = "Back",
                    Value = 8,
                    Doc = "Overshoots the far end and comes back. Values outside 0-1 are the point of it, so a "
                        .. "property tweened with this one has to tolerate them.",
                },
                { Name = "Bounce", Value = 9, Doc = "Settles by bouncing, like something dropped." },
                {
                    Name = "Elastic",
                    Value = 10,
                    Doc = "Oscillates past both ends before settling. Like `Back`, it leaves 0-1.",
                },
            } :: { Schema.EnumItem },
        },
        {
            Name = "EasingDirection",
            Doc = "Which end of a tween the easing applies to (§2.1).",
            Items = {
                { Name = "In", Value = 0, Doc = "Eased at the start, abrupt at the end." },
                { Name = "Out", Value = 1, Doc = "Abrupt at the start, eased at the end. The usual choice." },
                {
                    Name = "InOut",
                    Value = 2,
                    Doc = "Eased at both ends: the `In` curve over the first half, mirrored over the second.",
                },
            } :: { Schema.EnumItem },
        },
        {
            Name = "PlaybackState",
            Doc = "Where a `Tween` is in its life (§2.1). A state rather than a pair of booleans, because "
                .. '"finished" and "stopped early" are different facts and code that acts on one usually '
                .. "must not act on the other.",
            Items = {
                { Name = "Begin", Value = 0, Doc = "Created and never played." },
                {
                    Name = "Delayed",
                    Value = 1,
                    Doc = "Playing, inside its `TweenInfo.DelayTime`, and not yet writing anything.",
                },
                { Name = "Playing", Value = 2 },
                {
                    Name = "Paused",
                    Value = 3,
                    Doc = "Stopped where it was. `Play` resumes from there rather than restarting.",
                },
                {
                    Name = "Completed",
                    Value = 4,
                    Doc = "Reached its end. The goal values have been written exactly, not approached: a tween "
                        .. "that stopped at 0.9999 of the way would leave a property somebody has to explain.",
                },
                {
                    Name = "Cancelled",
                    Value = 5,
                    Doc = "Stopped before its end by `Cancel`. The property keeps whatever value it had reached "
                        .. "-- cancelling is not undoing.",
                },
            } :: { Schema.EnumItem },
        },
        {
            Name = "FillDirection",
            Doc = "Which way a `UIListLayout` stacks its siblings (§2.2).",
            Items = {
                { Name = "Horizontal", Value = 0 },
                { Name = "Vertical", Value = 1 },
            } :: { Schema.EnumItem },
        },
        {
            Name = "HorizontalAlignment",
            Doc = "Alignment along the X axis. One vocabulary for two jobs (§2.5, divergence #23): a "
                .. "`TextLabel` aligns its text with it and a `UIListLayout` aligns its children with it, and "
                .. "there is deliberately no `TextXAlignment` beside it.",
            Items = {
                { Name = "Left", Value = 0 },
                { Name = "Center", Value = 1 },
                { Name = "Right", Value = 2 },
            } :: { Schema.EnumItem },
        },
        {
            Name = "VerticalAlignment",
            Doc = "Alignment along the Y axis, and the same one vocabulary as `HorizontalAlignment`.",
            Items = {
                { Name = "Top", Value = 0 },
                { Name = "Center", Value = 1 },
                { Name = "Bottom", Value = 2 },
            } :: { Schema.EnumItem },
        },
        {
            Name = "SortOrder",
            Doc = "What order a `UIListLayout` lays its children out in (§2.2).",
            Items = {
                {
                    Name = "Name",
                    Value = 0,
                    Doc = "By `Name`, ordinal by code point. Stable across runs, which document order also is "
                        .. "-- the difference is that this one survives the children being rebuilt in a "
                        .. "different order.",
                },
                {
                    Name = "LayoutOrder",
                    Value = 1,
                    Doc = "By each child's `LayoutOrder`, ties broken by document order. The usual choice, and "
                        .. "the only one that lets a list be reordered without renaming anything.",
                },
            } :: { Schema.EnumItem },
        },
        {
            Name = "AutomaticSize",
            Doc = "Which axes a `UIObject` sizes to fit its contents on, overriding that half of `Size` "
                .. "(§2.2). Text and lists are what need it: a label whose box is smaller than its words is "
                .. "the commonest UI defect there is.",
            Items = {
                { Name = "None", Value = 0 },
                { Name = "X", Value = 1 },
                { Name = "Y", Value = 2 },
                { Name = "XY", Value = 3 },
            } :: { Schema.EnumItem },
        },
        {
            Name = "ScaleType",
            Doc = "How an `ImageLabel` fits its image into its box (§2.2).",
            Items = {
                {
                    Name = "Stretch",
                    Value = 0,
                    Doc = "The whole image into the whole box, aspect ratio and all.",
                },
                {
                    Name = "Slice",
                    Value = 1,
                    Doc = "Nine-slice: the four corners are drawn at their own size, the four edges stretch "
                        .. "along one axis and the middle stretches along both. `SliceCenter` says where the "
                        .. "cuts are. This is how a panel or a button keeps its rounded corners at any size.",
                },
                {
                    Name = "Tile",
                    Value = 2,
                    Doc = "Repeated at its own size until the box is full.",
                },
            } :: { Schema.EnumItem },
        },
    },
}

return Definition
