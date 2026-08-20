--!strict
--[[
	A project's own test.

	`tests/**/*.test.luau` is the user-project convention: `lute test` runs
	these pure, with no engine at all, and they are the right home for anything
	that does not need a world. The engine's own conformance suite is a
	different thing entirely -- `*.spec.luau`, in the engine repository, run by
	the headless runtime (api-design.md §4).

	`@std` resolves through the `std` alias in this project's `.luaurc`, which
	is what `lute setup --with-luaurc` writes and which points the ANALYZER at
	Lute's typedefs. The editor resolves `@std` and `@luaug` separately, through
	`.vscode/settings.json`, against the stubs `luaug setup` generates for the
	pinned engine. A comment cannot say so in the `.luaurc` itself: any key that
	file does not recognise is a hard error that aborts the whole require (U-42).
]]

local assert = require("@std/test/assert")
local greeting = require("../src/shared/greeting")
local test = require("@std/test")

test.suite("greeting", function(suite)
    suite:case("says something on a cold start", function()
        assert.that(#greeting.forProject() > 0, "a greeting is not empty")
    end)

    suite:case("has a different thing to say after a reload", function()
        assert.neq(greeting.afterReload(), greeting.forProject())
    end)
end)
