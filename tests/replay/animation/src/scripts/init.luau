--!strict
--- M6's animation gate: clip sampling covered by the replay hash.
---
--- **The pose does not live in the world, so the world hash cannot see it.** A
--- joint palette is `render`'s, and a determinism gate that hashed the ECS would
--- reproduce perfectly whatever animation did — including nothing. So this scene
--- takes the one thing the runtime publishes back into the tree,
--- `AnimationTrack.TimePosition`, and drives a part's `CFrame` from it.
---
--- That is not a trick. `TimePosition` is the clip's clock, it advances by
--- `FixedTimestep * Speed` per tick, and every joint's sample is a pure function
--- of it — so a hash that reproduces the time reproduces the pose. What it does
--- NOT cover is the sampling arithmetic itself, and that is covered where it can
--- be: sixteen cases in `luaug_render_tests` assert the palette matrix by hand,
--- and the skinned capture golden records the uploaded bytes.
---
--- Three tracks, deliberately: one at normal speed, one at double, and one
--- looping. The second is what catches a `Speed` that stopped scaling the clock,
--- and the third is what catches a loop that RESETS to zero instead of wrapping
--- — which loses a fraction of a tick each time round and drifts against
--- everything else in the scene.

local RunService = game:GetService("RunService")

local mesh = Instance.new("MeshPart")
mesh.Name = "Rig"
mesh.MeshContent = "asset://models/rig.gltf" :: Content
mesh.Anchored = true
mesh.Position = vector.create(0, 0, 0)
mesh.Parent = workspace

--- The three readouts. A part per track, whose position IS that track's clock,
--- so the world hash carries three clip clocks and nothing else has to change.
local function readout(name: string): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Anchored = true
    part.Size = vector.create(1, 1, 1)
    part.Parent = workspace
    return part
end

local plainOut = readout("PlainClock")
local fastOut = readout("FastClock")
local loopOut = readout("LoopClock")

local plain: AnimationTrack? = nil
local fast: AnimationTrack? = nil
local looped: AnimationTrack? = nil

local players: { AnimationPlayer } = {}
for index = 1, 3 do
    local player = Instance.new("AnimationPlayer")
    player.Name = `Player{index}`
    player.Parent = mesh
    table.insert(players, player)
end

local ticks = 0

RunService.Heartbeat:Connect(function()
    ticks += 1

    -- Loaded on the first tick the mesh has actually been read. A track made
    -- before then finds no skeleton, which its own doc says and which this scene
    -- would otherwise silently demonstrate.
    if plain == nil then
        local candidate = players[1]:LoadAnimation("Bend")
        if candidate.Length == 0 then
            return
        end
        plain = candidate
        candidate:Play()

        fast = players[2]:LoadAnimation("Bend")
        local fastTrack = fast :: AnimationTrack
        fastTrack.Speed = 2
        fastTrack:Play()

        looped = players[3]:LoadAnimation("Bend")
        local loopTrack = looped :: AnimationTrack
        loopTrack.Looped = true
        loopTrack:Play()
        return
    end

    -- The clocks, into the world. Scaled by a hundred so the hash sees the
    -- fraction of a tick a loop would lose if it reset instead of wrapping.
    plainOut.Position = vector.create(0, (plain :: AnimationTrack).TimePosition * 100, 0)
    fastOut.Position = vector.create(0, (fast :: AnimationTrack).TimePosition * 100, 0)
    loopOut.Position = vector.create(0, (looped :: AnimationTrack).TimePosition * 100, 0)

    if ticks == 880 then
        local plainTrack = plain :: AnimationTrack
        local fastTrack = fast :: AnimationTrack
        local loopTrack = looped :: AnimationTrack

        -- The clip is one second. By tick 880 the plain track has long since
        -- ended and is holding its last frame; the double-speed one ended twice
        -- as early; the looped one is still going round.
        local sampled = not plainTrack.Playing
            and not fastTrack.Playing
            and loopTrack.Playing
            and plainTrack.TimePosition == plainTrack.Length
            and loopTrack.TimePosition < loopTrack.Length

        if not sampled then
            warn(
                `animation gate: plain={plainTrack.TimePosition}/{plainTrack.Playing} `
                    .. `fast={fastTrack.TimePosition}/{fastTrack.Playing} `
                    .. `loop={loopTrack.TimePosition}/{loopTrack.Playing}`
            )
        end
        game:SetAttribute("AnimationSampled", sampled)
    end
end)
