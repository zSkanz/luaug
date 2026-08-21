--!strict
--- `AnimationPlayer` and `AnimationTrack`, from api-design.md §2.2.
---
--- **These cases run against a rig that is not there**, and that is what they
--- are for. The conformance suite is headless and loads no glTF, so every track
--- here has a length of zero and drives nothing -- which is exactly the state a
--- real game is in for the frames between creating a `MeshPart` and its file
--- being read, and the state in which a nil index would be a crash a player
--- sees. What a loaded clip does is asserted in C++ (`luaug_render_tests`,
--- sixteen cases over sampling, blending and the pose walk) and in the skinned
--- capture golden; what a MISSING one does can only be asserted from here.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local PhysicsService = game:GetService("PhysicsService")

local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

describe("AnimationPlayer", function()
    local mesh: MeshPart = nil :: any
    local player: AnimationPlayer = nil :: any

    beforeEach(function()
        mesh = Instance.new("MeshPart")
        mesh.Name = "AnimationSpecMesh"
        mesh.Anchored = true
        mesh.Parent = workspace
        player = Instance.new("AnimationPlayer")
        player.Parent = mesh
    end)

    afterEach(function()
        mesh:Destroy()
    end)

    it("is an ordinary instance that stores nothing of its own", function()
        expect(player.ClassName):ToBe("AnimationPlayer")
        expect(player:IsA("Instance")):ToBe(true)
        expect(player.Parent):ToBe(mesh)
    end)

    it("returns a track for a clip that is not there, rather than nil", function()
        -- The whole reason `LoadAnimation` cannot return an optional: a mesh
        -- whose file has not finished loading is the ordinary case, and a nil
        -- here would make an ordinary frame a crash.
        local track = player:LoadAnimation("NoSuchClip")
        expect(typeof(track)):ToBe("AnimationTrack")
        expect(track.Length):ToBe(0)
        expect(track.Playing):ToBe(false)
    end)

    it("takes a clip name, a fragment, or a URN whose path names its own mesh", function()
        -- All three spellings resolve; none of them raises. What differs is
        -- which file the name is looked up in, and with no file loaded they all
        -- find nothing -- which is the point: the parsing must not be what
        -- fails.
        expect(player:LoadAnimation("Walk").Length):ToBe(0)
        expect(player:LoadAnimation("#Walk").Length):ToBe(0)
        expect(player:LoadAnimation("asset://models/x.glb#Walk" :: Content).Length):ToBe(0)
    end)

    it("refuses a content that is not a string", function()
        -- A number is accepted, because Luau coerces one to a string for any C
        -- function that asks for one and this binding is not going to be the
        -- single place in the surface that disagrees. A table is not.
        expect(function()
            return (player :: any):LoadAnimation({})
        end):ToThrow()
    end)
end)

describe("AnimationTrack", function()
    local mesh: MeshPart = nil :: any
    local track: AnimationTrack = nil :: any

    beforeEach(function()
        mesh = Instance.new("MeshPart")
        mesh.Name = "AnimationSpecMesh"
        mesh.Anchored = true
        mesh.Parent = workspace
        local player = Instance.new("AnimationPlayer")
        player.Parent = mesh
        track = player:LoadAnimation("Idle")
    end)

    afterEach(function()
        mesh:Destroy()
    end)

    it("is a handle, not an Instance and not a value type", function()
        expect(typeof(track)):ToBe("AnimationTrack")
        -- No `Parent`, no `ClassName`, nothing a tree walk could find.
        expect(function()
            return (track :: any).Parent
        end):ToThrow("script.err.unknown_member")
    end)

    it("starts stopped, at weight one, at speed one, not looped", function()
        expect(track.Playing):ToBe(false)
        expect(track.Weight):ToBe(1)
        expect(track.Speed):ToBe(1)
        expect(track.Looped):ToBe(false)
        expect(track.TimePosition):ToBe(0)
    end)

    it("takes writes to Looped, Speed and Weight and reads them back", function()
        -- The first datatype in the surface with writable members, and the
        -- reason is what a handle IS: this write is visible to every holder of
        -- the handle, where a write to a `CFrame` would land on a copy.
        track.Looped = true
        track.Speed = 2.5
        track.Weight = 0.25
        expect(track.Looped):ToBe(true)
        expect(track.Speed):ToBe(2.5)
        expect(track.Weight):ToBe(0.25)
    end)

    it("refuses a write to a read-only member and to one that does not exist", function()
        expect(function()
            (track :: any).Playing = true
        end):ToThrow("script.err.unknown_member")
        expect(function()
            (track :: any).Nonsense = 1
        end):ToThrow("script.err.unknown_member")
    end)

    it("refuses a Looped that is not a boolean and a Speed that is not a number", function()
        expect(function()
            (track :: any).Looped = "yes"
        end):ToThrow()
        expect(function()
            (track :: any).Speed = "fast"
        end):ToThrow()
    end)

    it("plays and stops a clip that is not there without raising", function()
        -- A track with no clip is not an error state, it is a length of zero.
        track:Play()
        track:Play(0.25)
        step(4)
        track:Stop()
        track:Stop(0.25)
        expect(track.TimePosition):ToBe(0)
    end)

    it("has an Ended signal that connects like any other", function()
        local fired = 0
        local connection = track.Ended:Connect(function()
            fired += 1
        end)
        expect(typeof(connection)):ToBe("Connection")
        track:Play()
        step(4)
        -- Never fires, because there is no clip to reach the end of. `Ended` is
        -- a past-tense fact about finishing, and a track that never started
        -- cannot have finished.
        expect(fired):ToBe(0)
        connection:Disconnect()
    end)

    it("is the same signal object on every read, so connect-here disconnect-there works", function()
        expect(track.Ended):ToBe(track.Ended)
    end)

    it("keeps answering reads after its mesh is destroyed", function()
        mesh:Destroy()
        step(2)
        expect(track.Playing):ToBe(false)
        expect(track.Length):ToBe(0)
        -- Recreated for `afterEach`, which destroys it again -- destroying a
        -- destroyed instance is what this suite must not do.
        mesh = Instance.new("MeshPart")
        mesh.Parent = workspace
    end)
end)
