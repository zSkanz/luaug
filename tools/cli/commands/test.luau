--!strict
--[[
	`luaug test` — the conformance suite on the headless engine, in TAP.

	The engine writes a per-case JSON report and this reads it. It never parses
	the console: every line the engine prints is resolved through the catalog
	(ADR 0019), so a test runner that read the console would break the first
	time a locale was added — which is exactly the coupling the catalog exists
	to prevent (M3 brief, Decision 6).
]]

local fs = require("@std/fs")
local json = require("@std/json")
local process = require("@std/process")

local catalog = require("../catalog")
local output = require("../output")
local projectlib = require("../project")

local tr = catalog.tr

export type Options = {
    path: string,
    -- Where to write JUnit XML, if anywhere. TAP always goes to stdout.
    junit: string?,
    -- Where the engine drops its JSON. A temporary beside the suite by default.
    report: string?,
}

type Case = {
    name: string,
    ok: boolean,
    message: string?,
}

local function tapEscape(text: string): string
    -- A `#` in a TAP description starts a directive, and a newline ends the
    -- line. Neither can survive verbatim in a case name.
    return (string.gsub(string.gsub(text, "[\r\n]+", " "), "#", "＃"))
end

local function xmlEscape(text: string): string
    local escaped = string.gsub(text, "[&<>\"']", {
        ["&"] = "&amp;",
        ["<"] = "&lt;",
        [">"] = "&gt;",
        ['"'] = "&quot;",
        ["'"] = "&apos;",
    })
    return escaped
end

local function writeJunit(path: string, cases: { Case }, failed: number)
    local lines: { string } = {
        '<?xml version="1.0" encoding="UTF-8"?>',
        `<testsuites tests="{#cases}" failures="{failed}">`,
        `  <testsuite name="conformance" tests="{#cases}" failures="{failed}">`,
    }
    for _, case in cases do
        if case.ok then
            table.insert(lines, `    <testcase name="{xmlEscape(case.name)}"/>`)
        else
            table.insert(lines, `    <testcase name="{xmlEscape(case.name)}">`)
            table.insert(lines, `      <failure>{xmlEscape(case.message or "")}</failure>`)
            table.insert(lines, "    </testcase>")
        end
    end
    table.insert(lines, "  </testsuite>")
    table.insert(lines, "</testsuites>")
    fs.writeStringToFile(path, table.concat(lines, "\n") .. "\n")
end

local function run(options: Options): number
    local engine = projectlib.findEngine()
    if engine == nil then
        print(tr("cli.err.no_engine"))
        return 2
    end

    local suite = options.path
    if not fs.exists(suite) then
        print(tr("cli.err.not_a_project", { path = suite }))
        return 2
    end

    local reportPath = options.report
        or projectlib.joined(process.env.TMPDIR or process.env.TEMP or ".", "luaug-test-report.json")
    if fs.exists(reportPath) then
        -- A stale report from a previous run would let a crashed engine look
        -- like a passing suite -- the emptiness failure this milestone keeps
        -- designing against (M2 Finding 19).
        fs.remove(reportPath)
    end

    print(tr("cli.test.running", { path = suite }))
    local result = process.run({ engine :: string, `--run-tests={suite}`, `--test-report={reportPath}` }, {
        stdio = "inherit",
    })

    if not fs.exists(reportPath) then
        print(tr("cli.test.err.no_report", { path = reportPath }))
        return 1
    end

    local decoded = json.deserialize(fs.readFileToString(reportPath))
    local report = json.asObject(decoded)
    if report == nil then
        print(tr("cli.test.err.no_report", { path = reportPath }))
        return 1
    end

    local cases: { Case } = {}
    local rawCases = json.asArray(report.cases)
    if rawCases ~= nil then
        for _, entry in rawCases do
            local case = json.asObject(entry)
            if case ~= nil then
                table.insert(cases, {
                    name = tostring(case.name),
                    ok = case.ok == true,
                    message = if case.message ~= nil then tostring(case.message) else nil,
                })
            end
        end
    end

    if #cases == 0 then
        -- An empty suite passes forever, so this is a failure and not a result.
        print(tr("cli.test.err.no_cases", { path = suite }))
        return 1
    end

    -- TAP version 13, which is what every consumer of the format understands.
    -- Through `output.emit` rather than `print`, because this is a protocol
    -- for another program and has no business being translated -- and because
    -- the i18n lint tells the two apart by which function they went through.
    output.emit("TAP version 13")
    output.emit(`1..{#cases}`)
    local failed = 0
    for index, case in cases do
        if case.ok then
            output.emit(`ok {index} - {tapEscape(case.name)}`)
        else
            failed += 1
            output.emit(`not ok {index} - {tapEscape(case.name)}`)
            if case.message ~= nil then
                output.emit("  ---")
                output.emit(`  message: {tapEscape(case.message :: string)}`)
                output.emit("  ...")
            end
        end
    end

    if options.junit ~= nil then
        writeJunit(options.junit :: string, cases, failed)
        print(tr("cli.test.wrote_junit", { path = options.junit }))
    end

    print(tr("cli.test.summary", { total = #cases, passed = #cases - failed, failed = failed }))

    if failed > 0 then
        print(tr("cli.test.err.failed", { failed = failed, total = #cases }))
        return 1
    end
    return if result.ok then 0 else 1
end

return {
    run = run,
}
