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
    --- The base name of the hand-written C++ accessor pair backing this
    --- property: `Native = "basePartTransparency"` means the engine implements
    --- `getBasePartTransparency` and `setBasePartTransparency`, and the
    --- generator emits the declarations and wires the descriptor to them.
    ---
    --- This is the one place the IDL admits C++ exists, and it is deliberately
    --- a *name* rather than a storage expression: architecture.md §4 says
    --- hand-written C++ implements only the referenced native functions, so the
    --- generator stays ignorant of components, and moving a property's storage
    --- is a change in one C++ file rather than in the API definition.
    ---
    --- Absent means the accessor pair is named by the default rule --
    --- `<class><Property>` in lower camel -- and the property IS backed. The
    --- override exists for the cases where that name would be wrong.
    ---
    --- **`Native = ""` is how the IDL says "declared, not yet backed"**: it
    --- produces null accessors, reading yields nothing, and the generator counts
    --- them, so a surface shipping ahead of its implementation is a number in
    --- the generator's output rather than a discovery made by reading `nil` off
    --- a property. This paragraph said the opposite until M4.5 -- that *absent*
    --- meant unbacked -- which would have made every property in these files
    --- unbacked, including the ones a conformance suite exercises.
    Native: string?,
    --- Whether the property survives serialization; the default is that it
    --- does, so only the exceptions are marked.
    Transient: boolean?,
    --- **Backed, stored, read back faithfully -- and nothing acts on it yet.**
    ---
    --- Distinct from an absent `Native`, which means the property is not backed
    --- at all and reads as nothing. An inert property round-trips perfectly and
    --- changes nothing about the running engine, which is the harder case to
    --- notice: every way of checking it from a script agrees with what was
    --- written.
    ---
    --- It exists because all three of M4's unbacked-behaviour defects were found
    --- the same way -- a human changing a value in the inspector and watching
    --- nothing happen. The marker reaches `PropertyDesc` and the inspector shows
    --- it, so the panel can say "stored" where it would otherwise imply "done".
    ---
    --- A marker is not permission. Every entry here is a debt with a milestone
    --- attached, named in the property's own `Doc`.
    Inert: boolean?,
    --- Overrides the i18n key raised when a write is refused.
    ---
    --- Absent, the key is derived from the property's TYPE, which is right for
    --- a property whose only way to refuse is a wrong type and wrong for every
    --- property with a range: `PhysicsService.FixedTimestep = 1/10` answered
    --- "it takes a number" about a number for a whole milestone (D021). A
    --- setter returns a bool and has no way to say *which* of its conditions
    --- failed, so the key has to carry the whole domain rather than the
    --- failure.
    ---
    --- That is why the override reads as a domain and not as an error: a range
    --- key SUBSUMES the type key, because "it takes a number from 0 to 1" is
    --- equally true of `Restitution = "loud"`. Keys are shared by shape
    --- (`number_at_least_zero`) rather than minted per property, so a reader
    --- who has seen one has seen them all.
    ErrorKey: string?,
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
    --- The base name of the hand-written hooks that attach and detach this
    --- class's own component storage: `Storage = "Part"` means the engine
    --- implements `attachPartComponents` and `detachPartComponents`, and the
    --- generator wires the descriptor to them.
    ---
    --- Like `Property.Native` it is a *name* rather than a storage expression,
    --- which is what keeps the generator ignorant of what a component is
    --- (architecture.md §4).
    ---
    --- Only a class that DECLARES storage carries it. `World::create` walks the
    --- ancestry root-first and calls every hook it finds, so repeating a
    --- parent's name on a child would attach the same component twice. Absent
    --- means the class stores nothing of its own, which is most of them.
    Storage: string?,
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

--- An operator a value type supports: `CFrame * CFrame`, `Color3 == Color3`.
---
--- Spelled as the metamethod the VM actually dispatches on, so the name is
--- `__mul` rather than `Multiply` -- the one place the §9 casing rules do not
--- apply, because the name is not ours to choose. Several entries may share a
--- name: `CFrame * CFrame` and `CFrame * vector` are two overloads of one
--- operator, and the definition generator emits them as two declarations.
export type Operator = {
    Name: string,
    Parameters: { Parameter },
    Returns: { string },
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
    --- The metamethods the type declares. Without them the generated
    --- definitions have no `CFrame * CFrame`, and every spec that composes two
    --- transforms is a type error against an engine that supports it.
    Operators: { Operator }?,
    Doc: string,
}

--- A global library table: `task`, and in time whatever else §1.1 blesses.
---
--- Distinct from a `Datatype` because nothing constructs one and nothing has
--- its type -- `task` is reached by name and never held in a variable, so it has
--- functions and no members. Its functions are camelCase like every other
--- namespace function (ADR 0034).
export type Library = {
    Name: string,
    Functions: { Method },
    Doc: string,
}

export type Definition = {
    Classes: { Class }?,
    Enums: { Enum }?,
    Datatypes: { Datatype }?,
    Libraries: { Library }?,
    --- Free functions the engine adds to the global table: `warn`. `print` is
    --- Luau's own and is not ours to declare.
    Globals: { Method }?,
}

--- The complete `TMS` set at the pin (`VM/src/ltm.h:12`), minus the ones a value
--- type has no business declaring: `__index`/`__newindex`/`__namecall` are the
--- binding's own dispatch, `__metatable` and `__mode` are not operators, and
--- `__type` is what `typeof` reads. An operator outside this list is a
--- declaration for a metamethod the VM will never call.
local MetamethodNames: { [string]: boolean } = {
    ["__add"] = true,
    ["__sub"] = true,
    ["__mul"] = true,
    ["__div"] = true,
    ["__idiv"] = true,
    ["__mod"] = true,
    ["__pow"] = true,
    ["__unm"] = true,
    ["__concat"] = true,
    ["__len"] = true,
    ["__eq"] = true,
    ["__lt"] = true,
    ["__le"] = true,
    ["__call"] = true,
    ["__iter"] = true,
    ["__tostring"] = true,
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
--- All three are frozen public surface in §2.1 and §2.2, so the rule grows a
--- named exception rather than the names growing a suffix -- which is the same
--- trade the service list above makes. A new name landing here is a decision
--- someone has to make in this file, which is the point.
---
--- `FocusLost` is M6's, and it is the one that reads worst: "lost" is past
--- tense in English and not in this pattern's grammar. It stays because §2.2
--- names it, and because `FocusEnded` beside `Focused` would be two words for
--- one pair of facts.
---
--- `InputBegan` is M6's too, and it is here for the reason ADR 0041 exists: the
--- whole value of the surface is that a person arriving from a familiar platform
--- reaches for that exact name and finds it. `InputChanged` and `InputEnded`
--- already read as past-tense facts and need no exception; renaming only the
--- first of the three to `InputStarted` would break the set to satisfy a rule
--- the other two already satisfy.
local EventNameExceptions = { Heartbeat = true, MessageOut = true, FocusLost = true, InputBegan = true }

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
    -- `Inert` says "backed, stored, and nothing reads it". `Native = ""` says
    -- "not backed at all", which the generator already counts separately. Two
    -- markers on one property would leave a reader unable to tell which of the
    -- two claims it is making.
    if property.Inert == true and property.Native == "" then
        table.insert(errors, `{where}: Inert and Native="" are different claims -- an unbacked property stores nothing`)
    end
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
    -- A read-only property is never set, so an override on one is a key nothing
    -- can raise -- and it reads, to the next person, as though the property has
    -- a domain somebody has to satisfy.
    if property.ErrorKey ~= nil then
        if property.ReadOnly == true then
            table.insert(errors, `{where}: ErrorKey on a read-only property names a refusal that cannot happen`)
        end
        if string.match(property.ErrorKey, "^%l[%l%d_]*%.err%.[%l%d_]+$") == nil then
            table.insert(
                errors,
                `{where}: ErrorKey "{property.ErrorKey}" is not a <module>.err.<name> catalog key (§6)`
            )
        end
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
    -- Matched case-insensitively so that `task.wait` is covered by the same
    -- exception `WaitForChild` is. A namespace function is camelCase and a
    -- member is PascalCase (ADR 0034), so the two spellings of the same word
    -- are the only difference between them -- and the exception is about the
    -- word, not about where the capital falls.
    local saysSoAlready = string.lower(string.sub(method.Name, 1, 4)) == "wait"
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

    -- Hoisted into a typed local rather than iterated as `x or {}`: the new
    -- solver types that expression as a union with the empty table and refuses
    -- to iterate it. The same shape appears above for `valueProperties`.
    local libraries: { Library } = definition.Libraries or {}
    for _, library in libraries do
        -- A library is reached by name, so its own name is camelCase like the
        -- functions on it: `task.wait`, never `Task.Wait`.
        if string.match(library.Name, CamelCase) == nil then
            table.insert(errors, `{library.Name}: library names are camelCase (ADR 0034)`)
        end
        for _, fn in library.Functions do
            checkMethod(errors, library.Name, fn, true)
        end
    end

    local globals: { Method } = definition.Globals or {}
    for _, global in globals do
        if string.match(global.Name, CamelCase) == nil then
            table.insert(errors, `{global.Name}: global functions are camelCase (ADR 0034)`)
        end
        if global.Doc == "" then
            table.insert(errors, `{global.Name}: every member carries doc text (§5)`)
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
        local operators: { Operator } = datatype.Operators or {}
        for _, operator in operators do
            -- Checked against the VM's metamethod set rather than against the
            -- casing rules: the name is not ours to choose, and a typo here
            -- would emit a declaration for a metamethod that never dispatches.
            if not MetamethodNames[operator.Name] then
                table.insert(errors, `{datatype.Name}.{operator.Name}: not a metamethod this VM dispatches on (§2.3)`)
            end
            if operator.Doc == "" then
                table.insert(errors, `{datatype.Name}.{operator.Name}: every member carries doc text (§5)`)
            end
        end
    end

    return errors
end

return Schema
