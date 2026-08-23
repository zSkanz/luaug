--!strict
local PhysicsService = game:GetService("PhysicsService")

PhysicsService:RegisterCollisionGroup("Player")
PhysicsService:RegisterCollisionGroup("Debris")

-- Symmetric: setting a against b sets b against a.
PhysicsService:CollisionGroupSetCollidable("Player", "Debris", false)
