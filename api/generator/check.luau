--!strict
--[[
	API definition lint (api-design.md §5, §9).

	Loads the assembled definition and runs the schema's naming and structural
	rules over it. The type checker already validated the *shape* of every
	definition file -- that is why the IDL is Luau -- so what runs here is only
	what a type cannot express: casing, the `Async` biconditional, event tense,
	enum singularity, parent classes that exist.

	This is a gate rather than a report. §2.5's rename list is frozen, so a bad
	name that reaches a release is a name the engine keeps forever; catching it
	while the definition is still a diff costs nothing.

	    lute api/generator/check.luau
]]

local process = require("@std/process")

local Definition = require("../defs")
local Schema = require("../schema")

local Errors = Schema.validate(Definition)

local ClassCount = #(Definition.Classes or {})
local EnumCount = #(Definition.Enums or {})
local DatatypeCount = #(Definition.Datatypes or {})

if #Errors > 0 then
    for _, message in Errors do
        print(`apicheck: {message}`)
    end
    print(
        `apicheck: {#Errors} violation(s) across {ClassCount} class(es), {EnumCount} enum(s), {DatatypeCount} datatype(s)`
    )
    process.exit(1)
end

print(`apicheck: ok -- {ClassCount} class(es), {EnumCount} enum(s), {DatatypeCount} datatype(s)`)
