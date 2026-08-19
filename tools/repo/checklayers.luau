--!strict
--[[
	Module layering gate (architecture.md §2).

	CMake's luaug_add_module() already refuses a declared dependency at an
	equal-or-higher layer, but that only sees LINK edges. This reads the actual
	`#include "luaug/<module>/..."` lines, which is what catches a header
	sneaking across a seam without a link edge -- the way layering usually rots.

	    lute tools/repo/checklayers.luau
]]

local fs = require("@std/fs")
local path = require("@std/path")
local process = require("@std/process")

local ENGINE_ROOT = "engine"
local SCAN_SUFFIXES = { ".cpp", ".h" }

-- The layer matrix, transcribed from architecture.md §2. Backends sit at their
-- interface's layer; only `app` may link them (ADR 0023).
local LAYERS: { [string]: number } = {
    core = 0,

    jobs = 1,
    platform = 1,

    rhi_api = 2,
    rhi_sdlgpu = 2,
    rhi_capture = 2,
    rhi_null = 2,
    rhi_bgfx = 2,
    physics_api = 2,
    physics_jolt = 2,
    physics_box2d = 2,
    net_api = 2,
    net_gns = 2,
    net_enet = 2,
    audio = 2,
    asset = 2,

    scene = 3,

    render = 4,
    input = 4,
    nav = 4,

    ui = 5,
    script = 5,

    app = 6,
}

-- architecture.md §2 gives `script` a dependency on `ui`, which sits at the
-- same layer. It is the one documented same-layer edge; anything else at equal
-- layer is a cycle waiting to happen.
local SAME_LAYER_ALLOWED: { [string]: { [string]: boolean } } = {
    script = { ui = true },
}

local function fail(message: string): never
    print(`checklayers: ERROR: {message}`)
    return process.exit(1)
end

local function hasSuffix(text: string, suffix: string): boolean
    return #text >= #suffix and string.sub(text, -#suffix) == suffix
end

type Violation = {
    file: string,
    from: string,
    to: string,
}

local function scanModule(moduleName: string, moduleDir: string, violations: { Violation })
    local fromLayer = LAYERS[moduleName]

    local pending: { string } = { moduleDir }
    while #pending > 0 do
        local dir = table.remove(pending) :: string

        for _, entry in fs.listDirectory(dir) do
            local childPath = path.format(path.join(dir, entry.name))

            if entry.type == "dir" then
                table.insert(pending, childPath)
                continue
            end
            if entry.type ~= "file" then
                continue
            end

            local matched = false
            for _, suffix in SCAN_SUFFIXES do
                if hasSuffix(childPath, suffix) then
                    matched = true
                    break
                end
            end
            if not matched then
                continue
            end

            local source = fs.readFileToString(childPath)
            for included in string.gmatch(source, '#include%s*[<"]luaug/([%w_]+)/') do
                if included == moduleName then
                    continue
                end

                local toLayer = LAYERS[included]
                if not toLayer then
                    table.insert(violations, { file = childPath, from = moduleName, to = included })
                    continue
                end

                local allowed = toLayer < fromLayer
                if not allowed and toLayer == fromLayer then
                    local exceptions = SAME_LAYER_ALLOWED[moduleName]
                    allowed = exceptions ~= nil and exceptions[included] == true
                end

                if not allowed then
                    table.insert(violations, { file = childPath, from = moduleName, to = included })
                end
            end
        end
    end
end

local function main()
    if not fs.exists(ENGINE_ROOT) then
        fail(`{ENGINE_ROOT}/ not found — run this from the repository root`)
    end

    local violations: { Violation } = {}
    local checked = 0

    for _, entry in fs.listDirectory(ENGINE_ROOT) do
        if entry.type ~= "dir" then
            continue
        end

        local moduleName = entry.name
        if not LAYERS[moduleName] then
            fail(`engine/{moduleName}/ has no layer in architecture.md §2 — add it to LAYERS or to the docs`)
        end

        checked += 1
        scanModule(moduleName, path.format(path.join(ENGINE_ROOT, moduleName)), violations)
    end

    for _, violation in violations do
        local toLayer = LAYERS[violation.to]
        if toLayer then
            print(
                `checklayers: {violation.file}: {violation.from} (L{LAYERS[violation.from]}) includes {violation.to} (L{toLayer})`
            )
        else
            print(`checklayers: {violation.file}: {violation.from} includes unknown module "{violation.to}"`)
        end
    end

    print(`checklayers: {checked} module(s) checked, {#violations} violation(s)`)

    if #violations > 0 then
        fail(`{#violations} layering violation(s) — see architecture.md §2`)
    end
end

main()
