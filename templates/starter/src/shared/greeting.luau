--!strict
--[[
	A shared module.

	Anything outside `src/scripts` is a module: it never appears in the instance
	tree, it is evaluated once per VM, and it is reached by `require` through the
	`@shared` alias this project's `.luaurc` declares.
]]

local Greeting = {}

function Greeting.forProject(): string
    return "Hello from LuauG. Edit src/scripts/main.luau and save."
end

function Greeting.afterReload(): string
    return "...and that was a hot reload. The window never closed."
end

return Greeting
