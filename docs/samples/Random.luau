--!strict
-- A seeded stream replays; an ambient math.random does not.
local rng = Random.new(1234)

print(rng:NextInteger(1, 6))
print(rng:NextNumber(0, 1))
print(rng:NextUnitVector())
