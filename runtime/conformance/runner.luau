--!strict
--[[
	The conformance suite's runner (api-design.md §3).

	Mounted as an ordinary entry `Script` alongside every `*.spec.luau`, and it
	runs the suite once `game.Loaded` has fired -- so a spec that connects to
	`game.Loaded` at file scope has already been given its turn.

	A real file rather than a string inside `world_host.cpp`. Written that way
	first, and the escaping alone made it unreadable; as a file it is analysed
	by the gate, formatted by StyLua, and diffable like everything else.
]]

local testing = require("@luaug/testing")

--- A JSON encoder rather than `@std/json`, which the game VM does not carry yet
--- (api-design.md §7 schedules it; nothing before now has needed it). It only
--- ever sees the two shapes below.
local function quote(text: string): string
    local escaped = string.gsub(text, '[%c"\\]', function(c: string): string
        if c == '"' then
            return '\\"'
        elseif c == "\\" then
            return "\\\\"
        elseif c == "\n" then
            return "\\n"
        elseif c == "\r" then
            return "\\r"
        elseif c == "\t" then
            return "\\t"
        end
        return string.format("\\u%04x", string.byte(c) or 0)
    end)
    return `"{escaped}"`
end

game.Loaded:Connect(function()
    local report = testing.run()
    print(testing.format(report))

    -- The machine-readable half. `luaug test` turns this into TAP or JUnit; it
    -- never parses the console, because every line the engine prints is
    -- catalog-resolved and would change the first time a locale is added
    -- (M3 brief, Decision 6).
    local parts: { string } = {}
    for _, case in report.Cases do
        local fields = { `"name":{quote(table.concat(case.Path, " > "))}`, `"ok":{tostring(case.Ok)}` }
        if case.Message ~= nil then
            table.insert(fields, `"message":{quote(case.Message :: string)}`)
        end
        table.insert(parts, "{" .. table.concat(fields, ",") .. "}")
    end

    local summary = {
        `"total":{report.Total}`,
        `"passed":{report.Passed}`,
        `"failed":{report.Failed}`,
        `"cases":[{table.concat(parts, ",")}]`,
    }

    game:SetAttribute("ConformanceTotal", report.Total)
    game:SetAttribute("ConformancePassed", report.Passed)
    game:SetAttribute("ConformanceFailed", report.Failed)
    game:SetAttribute("ConformanceReport", "{" .. table.concat(summary, ",") .. "}")
    game:Shutdown()
end)
