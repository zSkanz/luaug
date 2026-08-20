--!strict
--- Enums (api-design.md §2.3, the Enum row).
---
--- Only the enums the M2 surface uses. The rest of the v1 list arrives with the
--- milestone that gives each one meaning; an enum nobody can pass anywhere is a
--- name squatting on a meaning we have not designed yet.
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
    },
}

return Definition
