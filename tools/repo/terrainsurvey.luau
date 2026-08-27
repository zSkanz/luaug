--!strict
--[[
	F1 step A5: does the hybrid terrain's slope precondition hold on real ground?

	**The design being measured.** F1 stores one signed-distance field two ways:
	a dense per-column height layer where the ground is a single-valued height
	function, and sparse voxel bricks where it is not. A column stays in the
	cheap encoding only while every boundary quad satisfies `|dH| <= voxelSize`
	between neighbouring samples -- which, over a spacing of exactly `voxelSize`,
	is the statement that the surface is no steeper than 45 degrees.

	**What it costs to be wrong.** If most ground promotes to bricks, the hybrid
	pays pure voxel's memory AND its destroy-and-recreate collider AND carries
	the extra code of a second encoding: the worst of both. That is why this
	measurement runs before the mesher is written rather than after.

	**Two fields, and the second one is the point.** The flagship's island is the
	only analytic ground this repository has, and it is DELIBERATELY GENTLE --
	`generate_world.luau` refuses to write a world whose step exceeds the
	character's `AutoStepHeight` of 0.6 m over 16 m tiles. Measuring only that
	would answer "the hybrid works on the one island we designed to be walkable",
	which is not the question. So a synthetic field with a cliff of controlled
	steepness runs beside it, and the report is a curve rather than a verdict:
	at what steepness does the cheap encoding stop paying?

	    lute tools/repo/terrainsurvey.luau
]]

local process = require("@std/process")

--- The voxel edge F1 samples on, in metres. The promotion test is scale-free --
--- `|dH| <= voxelSize` over a spacing of `voxelSize` is a slope of 1 whatever
--- the number is -- but it is named rather than inlined because every printed
--- height below is in these units.
local VoxelSize = 0.5

--- The flagship's field, transcribed from
--- `examples/10-open-world/tools/generate_world.luau`. Copied rather than
--- imported: that file is a SEED script that writes a scene when it runs, and
--- this one must be able to sample the field without producing a world.
---
--- **The header of that file already warns that this function is duplicated**
--- into `src/scripts/init.luau` and that the copies have to move together. This
--- is a third copy and it is measurement-only, so it is named here rather than
--- pretending otherwise.
local ShoreRadius = 900
local SeaLevel = 0
local PeakHeight = 46

local function smoothstep(t: number): number
    local c = math.clamp(t, 0, 1)
    return c * c * (3 - 2 * c)
end

--- Named so the band sweep below reads as what it is. A band is centred on the
--- origin, so it runs from `-halfOf(band)` to `+halfOf(band)`.
local function halfOf(width: number): number
    return width * 0.5
end

local function flagshipHeight(x: number, z: number): number
    local distance = math.sqrt(x * x + z * z)
    local land = smoothstep(1 - distance / ShoreRadius)
    local roll = 1.4 * math.sin(x / 400) * math.cos(z / 360)
    local seabed = -9 * smoothstep((distance - ShoreRadius) / 260)
    return SeaLevel + PeakHeight * land + roll * land + seabed
end

--- A field with a cliff whose steepness is an argument, so the answer is a curve.
---
--- The cliff is a smoothstep band 4 m wide rather than a step function: an
--- infinitely sharp edge promotes exactly one column however steep it is, which
--- would flatter the hybrid rather than test it. A band is what real sculpted
--- ground looks like and it spreads the steep region over several columns.
local function cliffField(dropMetres: number): (number, number) -> number
    return function(x: number, _z: number): number
        return dropMetres * smoothstep((x + 2) / 4)
    end
end

--- Fraction of neighbouring sample pairs whose height difference exceeds the
--- voxel size -- which is the fraction of the surface that would promote out of
--- the cheap encoding.
---
--- **Both axes and the diagonal**, for the reason `worstStep` gives in the
--- generator: a quad's boundary is not only its two axis-aligned edges, and a
--- measurement that skipped the diagonal would under-report every ridge running
--- at 45 degrees to the grid.
local function steepFraction(
    height: (number, number) -> number,
    halfExtent: number,
    waterline: number?
): (number, number)
    local steep = 0
    local total = 0
    local worst = 0

    local x = -halfExtent
    while x < halfExtent do
        local z = -halfExtent
        while z < halfExtent do
            local here = height(x, z)
            -- Only where a character can stand, when a waterline is given. The
            -- flagship's seabed drops fast ON PURPOSE -- that is what makes its
            -- offshore cells coarse -- and counting it would answer a question
            -- about the sea.
            if waterline == nil or here >= waterline then
                local deltas = {
                    math.abs(height(x + VoxelSize, z) - here),
                    math.abs(height(x, z + VoxelSize) - here),
                    math.abs(height(x + VoxelSize, z + VoxelSize) - here),
                }
                for _, delta in deltas do
                    total += 1
                    if delta > VoxelSize then
                        steep += 1
                    end
                    worst = math.max(worst, delta)
                end
            end
            z += VoxelSize
        end
        x += VoxelSize
    end

    if total == 0 then
        return 0, 0
    end
    return steep / total, worst
end

print("terrainsurvey: F1 step A5 -- the hybrid's slope precondition, measured")
print(
    string.format(
        "  voxel size %.2f m; a pair is STEEP when |dH| > %.2f m, i.e. slope > 45 degrees",
        VoxelSize,
        VoxelSize
    )
)
print("")

-- --- The real ground ------------------------------------------------------
--
-- Sampled over the island rather than the whole 2 km square: beyond the shore
-- the field is seabed, and the waterline filter would drop it anyway.
local flagshipSteep, flagshipWorst = steepFraction(flagshipHeight, ShoreRadius, SeaLevel)
print("  examples/10-open-world, above the waterline:")
print(string.format("    steep pairs      %.4f%%", flagshipSteep * 100))
print(string.format("    worst |dH|       %.4f m over %.2f m", flagshipWorst, VoxelSize))
print(string.format("    max slope        %.1f degrees", math.deg(math.atan(flagshipWorst / VoxelSize))))
print("")

-- --- The sensitivity curve ------------------------------------------------
print("  a 4 m cliff band, by how far it drops:")
print("    drop     steep pairs   worst |dH|   max slope")
for _, drop in { 1, 2, 4, 8, 16, 32, 64 } do
    local steep, worst = steepFraction(cliffField(drop), 8, nil)
    print(
        string.format(
            "    %5d m   %8.3f%%   %8.3f m   %6.1f deg",
            drop,
            steep * 100,
            worst,
            math.deg(math.atan(worst / VoxelSize))
        )
    )
end
print("")

-- --- The give-up rule, which is the number that actually decides the design --
--
-- F1 promotes outward until every boundary quad is shallow again and **gives up
-- at 8 columns**, converting the whole cell to bricks. At a 0.5 m voxel that is
-- a 4 m band -- so the question is not "how steep" but "how WIDE is the steep
-- part", and the curve above varied the wrong axis to answer it. A vertical
-- cliff is cheap: it is one column. A long ramp at 50 degrees is not.
print("  a 16 m drop, by how WIDE the band it falls over is:")
print("    band     steep columns   promotes?   (give-up is 8 columns = 4.00 m)")
for _, band in { 0.5, 1, 2, 4, 6, 8, 16 } do
    -- Count columns along x whose forward delta is steep, on the centre row.
    local steepColumns = 0
    local x = -halfOf(band) - 2
    local stop = halfOf(band) + 2
    local field = function(px: number, _pz: number): number
        return 16 * smoothstep((px + halfOf(band)) / band)
    end
    while x < stop do
        if math.abs(field(x + VoxelSize, 0) - field(x, 0)) > VoxelSize then
            steepColumns += 1
        end
        x += VoxelSize
    end
    print(
        string.format(
            "    %5.1f m   %11d     %s",
            band,
            steepColumns,
            if steepColumns <= 8 then "height layer survives" else "WHOLE CELL BRICKS"
        )
    )
end
print("")

-- --- The verdict, stated as the condition rather than as an opinion -------
--
-- One per cent is the line, and it is chosen rather than found: the hybrid's
-- whole claim is that the height layer carries the MAJORITY and bricks carry
-- the exceptions. A field that promotes one column in a hundred is paying for
-- the second encoding on one per cent of its area, which is the shape the
-- design assumed. A field that promotes one in five is not.
local Threshold = 0.01
if flagshipSteep <= Threshold then
    print(
        string.format(
            "  VERDICT: the precondition HOLDS on this field (%.4f%% <= %.1f%%).",
            flagshipSteep * 100,
            Threshold * 100
        )
    )
else
    print(
        string.format(
            "  VERDICT: the precondition FAILS on this field (%.4f%% > %.1f%%).",
            flagshipSteep * 100,
            Threshold * 100
        )
    )
end
print("")
print("  READ THE CURVE BEFORE TRUSTING THE VERDICT. The flagship's field is")
print("  constrained by the character's step height and is the gentlest ground")
print("  this repository has. The curve is what says how the hybrid behaves on")
print("  ground somebody sculpts a cliff into, which is the case F1 exists for.")

process.exit(0)
