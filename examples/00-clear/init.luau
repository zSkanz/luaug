--!strict

-- examples/00-clear — the M1 deliverable, driven from Luau.
--
-- The engine draws nothing on its own here: the clear colour and all three
-- cubes come from `onFrame` below. Delete the callback and the frame goes back
-- to the engine's own default scene, which is how you can tell this script is
-- really the thing driving it.
--
-- `luaug` is a TEMPORARY global, and deliberately an ugly one. M2 replaces the
-- whole surface with `require("@luaug/...")` over real services, Instances and
-- deferred signals. Nothing here should be copied into a real project — it
-- exists so that "a Luau script drives the frame" is true in M1 rather than
-- promised.

local ORBIT_RADIUS = 2.5
local HALF_EXTENT = 0.4
local THIRD_TURN = 2 * math.pi / 3

-- Fixed and saturated rather than derived from the animation. A colour that
-- follows the same wave as the background eventually matches it, and a debug
-- cube you cannot see is worse than no cube at all.
local CUBE_COLORS = {
    { 1.0, 0.15, 0.15 },
    { 0.15, 0.4, 1.0 },
    { 1.0, 1.0, 1.0 },
}

luaug.OnFrame(function(tick: number, elapsed: number)
    -- Driven by the tick count, never by wall-clock time. A headless capture
    -- has to produce the same pixels on any machine at any speed, which is the
    -- property the whole golden-image gate rests on.
    luaug.SetClearColor(
        0.5 + 0.5 * math.sin(elapsed),
        0.5 + 0.5 * math.sin(elapsed + 2.0944),
        0.5 + 0.5 * math.sin(elapsed + 4.1888)
    )

    for index, color in CUBE_COLORS do
        local phase = elapsed + (index - 1) * THIRD_TURN
        luaug.WireBox(
            ORBIT_RADIUS * math.cos(phase),
            0,
            ORBIT_RADIUS * math.sin(phase),
            HALF_EXTENT,
            HALF_EXTENT,
            HALF_EXTENT,
            color[1],
            color[2],
            color[3]
        )
    end

    -- A world triad, so a wrong view or projection matrix is obvious rather
    -- than merely suspicious.
    luaug.Axes(1.0)

    -- `tick` is the fixed-step counter and `elapsed` is tick * fixedDt. Both
    -- are passed because M2's scheduler will hand real scripts the same pair,
    -- and a script written against them now keeps working.
    local _ = tick
end)
