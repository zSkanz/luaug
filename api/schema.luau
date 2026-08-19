--!strict
--- The API definition IDL's schema (api-design.md §5, architecture.md §4).
---
--- `api/defs/*.api.luau` are data files written against these types, and the
--- Luau type checker validates their *shape* for free — that is the whole
--- reason the IDL is Luau rather than JSON or YAML. What the checker cannot
--- express, `Validate` below enforces: the naming rules of api-design.md §9,
--- which are a CI gate precisely so the public surface cannot drift one
--- property at a time.
---
--- Everything downstream is generated from these files and nothing else:
--- the C++ `ClassDescriptor` tables `scene::ClassRegistry` consumes, the
--- `declare extern type` definitions the analyzer and luau-lsp read, and later
--- the docs JSON and the api-dump. Hand-editing a generated file is the one
--- thing that breaks the arrangement, so CI diffs them.

local Schema = {}

--- Where a member may be touched from, once actor VMs exist (architecture §3).
--- v1 runs everything serially on the game VM, but the annotation is carried
--- from the first definition so that code written today survives the change.
--- The four levels are architecture.md §3's, not a paraphrase of them: an
--- earlier draft of this schema declared three (`Safe | ReadSafe | Unsafe`) and
--- the mismatch was caught the first time someone tried to annotate a member
--- and found the two documents using different alphabets.
export type ThreadSafety = "Unsafe" | "ReadParallel" | "LocalSafe" | "Safe"

--- `Service` is a singleton reached through `game:GetService`; `Abstract` has no
--- instances of its own; `NotCreatable` refuses `Instance.new`; `DevOnly` is
--- compiled out of shipping builds. A class may carry several.
export type ClassTag = "Service" | "Abstract" | "NotCreatable" | "DevOnly"

--- A parameter or return type is written as the Luau type the user sees --
--- `"string"`, `"vector"`, `"Instance?"`, `"{BasePart}"`, `"Enum.PartShape"`.
--- The generators map it to C++; the analyzer sees it verbatim.
export type TypeName = string

export type Parameter = {
    Name: string,
    Type: TypeName,
    --- Literal Luau source for the default, e.g. `"nil"` or `"1/60"`. Present
    --- exactly when the parameter is optional.
    Default: string?,
}

export type Property = {
    Name: string,
    Type: TypeName,
    ReadOnly: boolean?,
    Default: string?,
    ThreadSafety: ThreadSafety?,
    --- Whether the property survives serialization; the default is that it
    --- does, so only the exceptions are marked.
    Transient: boolean?,
    Doc: string,
}

export type Method = {
    Name: string,
    Parameters: { Parameter },
    Returns: { TypeName },
    --- A yielding method parks its caller. §9 requires such a method's name to
    --- end in `Async`, and `Validate` enforces the biconditional in both
    --- directions -- a non-yielding `Async` is as much a lie as a yielding one
    --- without it.
    Yields: boolean?,
    ThreadSafety: ThreadSafety?,
    Doc: string,
}

export type Event = {
    Name: string,
    Parameters: { Parameter },
    Doc: string,
}

export type Class = {
    Name: string,
    --- Absent only for `Instance` itself, the root of the hierarchy.
    Extends: string?,
    --- The engine module that registers this class into the scene registry
    --- (`scene`, `render`, `ui`, `input`, `audio`). Drives which generated
    --- file the descriptor lands in, so a milestone's classes arrive with the
    --- module that gives them meaning.
    Module: string,
    Tags: { ClassTag }?,
    --- The `Name` a fresh instance carries. Defaults to the class name.
    DefaultName: string?,
    Properties: { Property }?,
    Methods: { Method }?,
    Events: { Event }?,
    Doc: string,
}

export type EnumItem = {
    Name: string,
    Value: number,
    Doc: string?,
}

export type Enum = {
    Name: string,
    Items: { EnumItem },
    Doc: string,
}

--- A value type rather than an Instance: `CFrame`, `Color3`, `Random`,
--- `Signal`. Constructors are namespace functions and therefore camelCase
--- (`new`, `fromEuler`), while members reached through a value are PascalCase
--- (`:Inverse()`, `.Magnitude`) -- ADR 0034, and `Validate` holds the line.
export type Datatype = {
    Name: string,
    Module: string,
    --- Namespace-level functions: `CFrame.new`, `Color3.fromRGB`.
    Constructors: { Method }?,
    --- Namespace-level constants: `CFrame.identity`, `vector.zero`.
    Constants: { Property }?,
    --- Members reached through a value of this type.
    Properties: { Property }?,
    Methods: { Method }?,
    Doc: string,
}

export type Definition = {
    Classes: { Class }?,
    Enums: { Enum }?,
    Datatypes: { Datatype }?,
}

local PascalCase = "^%u[%w]*$"
local CamelCase = "^%l[%w]*$"

--- Events are past-tense facts or phase markers (§9). The suffix list is
--- deliberately short: a name that needs a new suffix is usually a name that
--- wants rethinking, and adding one here is a visible decision.
local EventSuffixes = { "ed", "ing" }
local EventPrefixes = { "Pre", "Post" }

--- Services whose names do not end in `Service`, per §9. Anything else ending
--- differently is a lint failure rather than a judgement call.
local ServiceNameExceptions = { Workspace = true, Lighting = true }

--- Events whose names are neither past-tense facts nor `Pre*`/`Post*` phases.
--- Both are frozen public surface in §2.1, so the rule grows a named exception
--- rather than the names growing a suffix -- which is the same trade the
--- service list above makes. A new name landing here is a decision someone has
--- to make in this file, which is the point.
local EventNameExceptions = { Heartbeat = true, MessageOut = true }

local function isPascalCase(name: string): boolean
    return string.match(name, PascalCase) ~= nil
end

local function endsWithAny(name: string, suffixes: { string }): boolean
    for _, suffix in suffixes do
        if #name > #suffix and string.sub(name, -#suffix) == suffix then
            return true
        end
    end
    return false
end

local function startsWithAny(name: string, prefixes: { string }): boolean
    for _, prefix in prefixes do
        if string.sub(name, 1, #prefix) == prefix then
            return true
        end
    end
    return false
end

local function isBoolean(typeName: TypeName): boolean
    return typeName == "boolean" or typeName == "boolean?"
end

--- The one documented casing exception (§9, divergence #9): `Vector3` IS the
--- Luau `vector` primitive, and `x`/`y`/`z` are the primitive's own fields --
--- fields of the language, like `string`, not members of a type we define.
--- Spelled as an explicit set rather than as "lowercase on Vector3 is fine", so
--- that a fourth lowercase field would still have to be argued for.
local PrimitiveFields: { [string]: boolean } = {
    ["Vector3.x"] = true,
    ["Vector3.y"] = true,
    ["Vector3.z"] = true,
}

local function checkProperty(errors: { string }, owner: string, property: Property)
    local where = `{owner}.{property.Name}`
    if not isPascalCase(property.Name) and not PrimitiveFields[where] then
        table.insert(errors, `{where}: property names are PascalCase (§9)`)
    end
    -- A property that reads as a call is a property somebody will call.
    if string.match(property.Name, "^Get%u") then
        table.insert(errors, `{where}: properties do not carry a Get prefix (§9)`)
    end
    if isBoolean(property.Type) and string.match(property.Name, "^Is%u") then
        table.insert(errors, `{where}: boolean properties have no Is prefix; boolean methods do (§9)`)
    end
    if property.Doc == "" then
        table.insert(errors, `{where}: every member carries doc text, which becomes its i18n key (§5)`)
    end
end

local function checkMethod(errors: { string }, owner: string, method: Method, namespaceLevel: boolean)
    local where = `{owner}.{method.Name}`
    -- Namespace-level entries (constructors, factories) are camelCase; members
    -- reached through an object are PascalCase. ADR 0034.
    if namespaceLevel then
        if string.match(method.Name, CamelCase) == nil then
            table.insert(errors, `{where}: namespace functions are camelCase (ADR 0034)`)
        end
    elseif not isPascalCase(method.Name) then
        table.insert(errors, `{where}: methods reached through a value are PascalCase (ADR 0034)`)
    end

    local named = endsWithAny(method.Name, { "Async" })
    local yields = method.Yields == true
    -- `Wait*` is the documented exception (§9). The `Async` suffix exists to
    -- warn that a call parks when its name does not say so; `WaitForChild`
    -- already says so, and appending `Async` to a name built around the word
    -- "wait" would be noise rather than a warning.
    local saysSoAlready = string.sub(method.Name, 1, 4) == "Wait"
    if yields and not (named or saysSoAlready) then
        table.insert(errors, `{where}: a yielding method's name ends in Async (§9)`)
    elseif named and not yields then
        table.insert(errors, `{where}: named Async but does not yield -- the suffix is a promise (§9)`)
    end

    local seenOptional = false
    for _, parameter in method.Parameters do
        if parameter.Default ~= nil then
            seenOptional = true
        elseif seenOptional then
            -- Luau has no way to skip an argument, so a required parameter
            -- after an optional one is unreachable except by passing nil.
            table.insert(errors, `{where}: required parameter "{parameter.Name}" follows an optional one`)
        end
        if string.match(parameter.Name, CamelCase) == nil then
            table.insert(errors, `{where}: parameter "{parameter.Name}" is camelCase (§9)`)
        end
    end

    if method.Doc == "" then
        table.insert(errors, `{where}: every member carries doc text (§5)`)
    end
end

local function checkEvent(errors: { string }, owner: string, event: Event)
    local where = `{owner}.{event.Name}`
    if not isPascalCase(event.Name) then
        table.insert(errors, `{where}: event names are PascalCase (§9)`)
    end
    local wellFormed = endsWithAny(event.Name, EventSuffixes)
        or startsWithAny(event.Name, EventPrefixes)
        or EventNameExceptions[event.Name] == true
    if not wellFormed then
        table.insert(errors, `{where}: events are past-tense facts or Pre*/Post* phases (§9)`)
    end
    if event.Doc == "" then
        table.insert(errors, `{where}: every member carries doc text (§5)`)
    end
end

--- `x or {}` produces a `{T} | {}` union that the new solver will not iterate,
--- so every optional list is bound to a typed local first. It reads as
--- ceremony and is the difference between compiling and not.
local function hasTag(class: Class, tag: ClassTag): boolean
    local tags: { ClassTag } = class.Tags or {}
    for _, entry in tags do
        if entry == tag then
            return true
        end
    end
    return false
end

--- Returns every naming and structural violation in `definition`, newest rule
--- last. An empty result is the gate passing; the generator refuses to emit
--- anything while this is non-empty, because a surface that ships one bad name
--- has to keep it forever (§2.5's rename list is frozen).
function Schema.validate(definition: Definition): { string }
    local errors: { string } = {}
    local classNames: { [string]: boolean } = {}
    local classes: { Class } = definition.Classes or {}
    local enums: { Enum } = definition.Enums or {}
    local datatypes: { Datatype } = definition.Datatypes or {}

    for _, class in classes do
        classNames[class.Name] = true
        if not isPascalCase(class.Name) then
            table.insert(errors, `{class.Name}: class names are PascalCase (§9)`)
        end
        if hasTag(class, "Service") and not ServiceNameExceptions[class.Name] then
            if not endsWithAny(class.Name, { "Service" }) then
                table.insert(errors, `{class.Name}: services end in Service, except Workspace and Lighting (§9)`)
            end
        end
        if class.Extends == nil and class.Name ~= "Instance" then
            table.insert(errors, `{class.Name}: only Instance may omit Extends`)
        end
        local properties: { Property } = class.Properties or {}
        local methods: { Method } = class.Methods or {}
        local events: { Event } = class.Events or {}
        for _, property in properties do
            checkProperty(errors, class.Name, property)
        end
        for _, method in methods do
            checkMethod(errors, class.Name, method, false)
        end
        for _, event in events do
            checkEvent(errors, class.Name, event)
        end
    end

    -- Resolved after the first pass so that declaration order in the def files
    -- does not have to match the hierarchy.
    for _, class in classes do
        local parent = class.Extends
        if parent ~= nil and not classNames[parent] then
            table.insert(errors, `{class.Name}: extends "{parent}", which no definition declares`)
        end
    end

    for _, enum in enums do
        if not isPascalCase(enum.Name) then
            table.insert(errors, `Enum.{enum.Name}: enum names are PascalCase (§9)`)
        end
        -- Singular, because `Enum.EasingStyles.Quad` reads as a set of styles
        -- rather than one style.
        if string.sub(enum.Name, -1) == "s" and string.sub(enum.Name, -2) ~= "ss" then
            table.insert(errors, `Enum.{enum.Name}: enum names are singular (§9)`)
        end
        local seenValues: { [number]: string } = {}
        for _, item in enum.Items do
            if not isPascalCase(item.Name) then
                table.insert(errors, `Enum.{enum.Name}.{item.Name}: enum items are PascalCase (§9)`)
            end
            local previous = seenValues[item.Value]
            if previous ~= nil then
                -- Values are serialized and compared; two names for one value
                -- makes a round trip lossy.
                table.insert(errors, `Enum.{enum.Name}: {item.Name} and {previous} share the value {item.Value}`)
            end
            seenValues[item.Value] = item.Name
        end
    end

    for _, datatype in datatypes do
        if not isPascalCase(datatype.Name) then
            table.insert(errors, `{datatype.Name}: datatype names are PascalCase (§9)`)
        end
        local constructors: { Method } = datatype.Constructors or {}
        local constants: { Property } = datatype.Constants or {}
        local valueProperties: { Property } = datatype.Properties or {}
        local valueMethods: { Method } = datatype.Methods or {}
        for _, constructor in constructors do
            checkMethod(errors, datatype.Name, constructor, true)
        end
        for _, constant in constants do
            -- Namespace constants are camelCase like the functions beside them:
            -- `CFrame.identity`, `vector.zero` (ADR 0034).
            if string.match(constant.Name, CamelCase) == nil then
                table.insert(errors, `{datatype.Name}.{constant.Name}: namespace constants are camelCase (ADR 0034)`)
            end
            -- They carry doc text like every other member. Reached through the
            -- casing branch above rather than through `checkProperty`, which
            -- would re-impose PascalCase -- and skipping the doc check with it
            -- was how `CFrame.identity` could have shipped undocumented.
            if constant.Doc == "" then
                table.insert(errors, `{datatype.Name}.{constant.Name}: every member carries doc text (§5)`)
            end
        end
        for _, property in valueProperties do
            checkProperty(errors, datatype.Name, property)
        end
        for _, method in valueMethods do
            checkMethod(errors, datatype.Name, method, false)
        end
    end

    return errors
end

return Schema
