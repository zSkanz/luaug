--!strict
local cf = CFrame.new(vector.create(0, 5, 0))
local turned = cf * CFrame.fromEuler(0, math.rad(90), 0)

print(turned.LookVector) -- the basis's -Z column: forward
print(turned * vector.create(0, 0, -1)) -- transforms a POINT
