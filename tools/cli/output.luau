--!strict
--[[
	Machine output.

	The distinction this module exists to make is the one R3 turns on. A message
	to a person goes through the catalog and is written with `print(tr(...))`; a
	line of TAP, or a row of JUnit, is bytes for another program and has no
	business being translated -- `ok 3 - …` is a protocol, not prose.

	Separating them by FUNCTION rather than by intention is what lets the i18n
	lint check the difference: `print` may not carry a string literal, and this
	may carry nothing else.
]]

local function emit(text: string)
    print(text)
end

return {
    emit = emit,
}
