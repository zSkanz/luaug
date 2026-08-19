--!strict

-- M0 smoke script. It proves three things the rest of the engine is built on:
-- the host can compile and run Luau, `print` reaches the engine log, and the
-- sandbox is genuinely on (rule R4) rather than merely intended.

print("boot.luau: hello from Luau")

local function mutateBuiltin()
    local lib: any = string
    lib.rep = nil
end

assert(not (pcall(mutateBuiltin)), "sandbox must prevent mutating builtin library tables")

print("boot.luau: sandbox verified")
