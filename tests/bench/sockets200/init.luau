--!strict
--- What a world full of joints costs, measured rather than estimated (E9).
---
--- **The constraint family added three per-tick passes that did not exist**, and
--- each of them is a walk over a pool rather than a solver step, so a scene with
--- no joints in it must still be able to prove it pays nothing while a scene
--- full of them must be affordable:
---
---   * attachment resolution -- every `Attachment` recomputes its world frame,
---     interleaved with weld resolution through the same memo recursion because
---     a bone's frame can depend on a part a weld drives;
---   * a second pool-order walk inside `applyScene`, after bodies, creating and
---     retiring constraints by the same mark-and-sweep the bodies use;
---   * and the solver's own cost, which is the only one anybody expects.
---
--- **Two hundred sockets, four hundred bodies, four hundred attachments.** Each
--- one is an anchored post with a free arm hung off it by a `BallSocketConstraint`
--- -- the joint a ragdoll is made of, and the one whose limited form is a
--- different solver joint rather than a free one with corrections bolted on. Half
--- of them are limited and half are not, because those are two different code
--- paths in the backend and a bench that exercised one would price the other by
--- accident.
---
--- **Nothing is written per tick.** That is deliberate and it is what separates
--- this row from `platforms200`: no script traffic, no property churn, no
--- kinematic transitions. What is left is the joints, so the number is the
--- joints.
---
--- The 16 ms budget is a whole frame and a catastrophe detector, like every other
--- scene here. The number that matters is the one recorded in
--- `docs/perf-baselines.md`.

local Root = Instance.new("Folder")
Root.Name = "Sockets"
Root.Parent = workspace

local ground = Instance.new("Part")
ground.Name = "Ground"
ground.Size = vector.create(400, 4, 400)
ground.Position = vector.create(0, -2, 0)
ground.Anchored = true
ground.Parent = Root

local function attachmentOn(host: BasePart, offset: CFrame): Attachment
    local point = Instance.new("Attachment")
    point.CFrame = offset
    point.Parent = host
    return point
end

--- One post, one arm, two attachments, one joint. Spread over a grid so the
--- broadphase has real work rather than four hundred bodies in one cell.
local function socket(index: number, limited: boolean)
    local column = index % 20
    local row = math.floor(index / 20)
    local x = column * 8 - 80
    local z = row * 8 - 40

    local post = Instance.new("Part")
    post.Name = `Post{index}`
    post.Size = vector.create(1, 6, 1)
    post.Position = vector.create(x, 6, z)
    post.Anchored = true
    post.Parent = Root

    local arm = Instance.new("Part")
    arm.Name = `Arm{index}`
    arm.Size = vector.create(1, 4, 1)
    -- Started off the vertical, so every joint has somewhere to fall and the
    -- solver is doing work from the first tick rather than settling instantly.
    arm.Position = vector.create(x + 2, 8, z)
    arm.Parent = Root

    local socketPoint = attachmentOn(post, CFrame.new(0, 3, 0))
    local armPoint = attachmentOn(arm, CFrame.new(0, 2, 0))

    local joint = Instance.new("BallSocketConstraint")
    joint.Attachment0 = socketPoint
    joint.Attachment1 = armPoint
    if limited then
        -- The limited form is a swing-twist joint in the backend rather than a
        -- point one, which is a different solver constraint and a different
        -- cost. Half and half, so the row prices both.
        joint.LimitsEnabled = true
        joint.UpperAngle = 40
        joint.TwistLimit = 15
    end
    joint.Parent = Root
end

for index = 1, 200 do
    socket(index, index % 2 == 0)
end
