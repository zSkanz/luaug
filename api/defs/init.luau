--!strict
--- The API definition, assembled (api-design.md §5).
---
--- Every generator reads this one module, so the list below is the registry of
--- what the public surface consists of. A definition file that is not required
--- here does not exist as far as the engine, the type definitions, the docs and
--- the api-dump are concerned -- which is deliberate: adding a class is meant
--- to be a visible edit in a file people review, not a directory scan that
--- picks up whatever happens to be lying around.

-- `@self` rather than `./`: inside an `init.luau`, `./` means the directory
-- *containing* this module's directory, which is how a folder module can reach
-- its siblings. Its own contents are `@self`. Getting this backwards resolves
-- to plausible-looking paths one level too high.
local Datatypes = require("@self/datatypes.api")
local Enums = require("@self/enums.api")
local Instances = require("@self/instances.api")
local Libraries = require("@self/libraries.api")
local Schema = require("./schema")
local Services = require("@self/services.api")

type Definition = Schema.Definition

local Sources: { Definition } = { Instances, Services, Datatypes, Enums, Libraries }

local function appendAll<T>(target: { T }, source: { T }?): ()
    if source == nil then
        return
    end
    for _, entry in source do
        table.insert(target, entry)
    end
end

local Classes: { Schema.Class } = {}
local Enumerations: { Schema.Enum } = {}
local DatatypeList: { Schema.Datatype } = {}
local LibraryList: { Schema.Library } = {}
local GlobalList: { Schema.Method } = {}

for _, source in Sources do
    appendAll(Classes, source.Classes)
    appendAll(Enumerations, source.Enums)
    appendAll(DatatypeList, source.Datatypes)
    appendAll(LibraryList, source.Libraries)
    appendAll(GlobalList, source.Globals)
end

local Definition: Definition = {
    Classes = Classes,
    Enums = Enumerations,
    Datatypes = DatatypeList,
    Libraries = LibraryList,
    Globals = GlobalList,
}

return Definition
