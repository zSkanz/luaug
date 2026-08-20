--!strict
--- Ring geometry, in `src/shared` so the example is a project rather than a
--- file with a directory around it: this module is reached as `@shared/ring`,
--- which is a `.luaurc` alias resolved by the engine's own `require` (§4).
---
--- It is deliberately pure. A module holds no Instances, connects no signals and
--- runs no `task` -- entry scripts do those things, and keeping the split
--- visible in the example is worth more than saving one file.

local Ring = {}

--- Where cube `index` of `count` sits on ring `ring`, at time `phase`.
---
--- One ring per Model, concentric, each turning at its own rate. Concentric
--- rather than one shared circle for a reason you can see: 500 cubes on a single
--- circle are closer together than they are wide, and read as a ribbon rather
--- than as 500 objects. Five rings of a hundred read as five rings of a hundred,
--- and a Model that leaves takes a whole ring with it.
function Ring.slotPosition(ring: number, index: number, count: number, phase: number): vector
    local radius = 1.7 + (ring - 1) * 0.8
    -- Alternating direction and a rate that falls with radius, so neighbouring
    -- rings shear against each other instead of turning as one disc.
    local spin = if ring % 2 == 0 then -1.0 else 1.0
    local angle = (index / count) * math.pi * 2 + phase * spin * (0.9 - ring * 0.1)
    -- A shallow wave with three periods around the ring: enough that the shape
    -- is never mistaken for a still image, small enough that the rings stay
    -- separable from the fixed camera.
    local height = math.sin(angle * 3.0 + phase * 1.7 + ring) * 0.28
    -- `vector`, not `Vector3`. They are the same values -- `Vector3` is a
    -- superset table over the builtin (§2.3) -- but `Part.Position` is typed as
    -- the builtin, and a module that promises the alias makes every caller cast.
    return vector.create(math.cos(angle) * radius, height, math.sin(angle) * radius)
end

return Ring
