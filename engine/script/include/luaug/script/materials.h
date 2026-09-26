// `Material`, the Luau data type (ADR 0090), and a part's parameters.
//
// A material is not an `Instance`: nothing parents one and nothing finds one in
// a `Workspace`. The handle is a userdata naming an asset's URN -- the shared,
// read-only asset -- or a runtime clone in the world the VM runs over, which is
// writable, never saved, and released when nothing points at it. The handle
// takes a hold on a clone for as long as it lives, so a clone a script keeps in
// a local survives the sweep that drops clones nothing wears.
#pragma once

#include "luaug/asset/material.h"
#include "luaug/scene/value.h"

#include <optional>

struct lua_State;

namespace luaug::script {

void registerMaterialTypes(lua_State* L);

// A `Material` handle for what a part wears.
void pushMaterial(lua_State* L, const scene::MaterialRef& material);
// The handle at `index`, or nothing for anything that is not one.
[[nodiscard]] std::optional<scene::MaterialRef> toMaterial(lua_State* L, int index);

// A part's overrides as a fresh table keyed by parameter name.
void pushMaterialParameters(lua_State* L, const asset::MaterialOverrides& overrides);
// A table of parameters, or nothing for one with a name that is not a
// parameter or a value of the wrong kind.
[[nodiscard]] std::optional<asset::MaterialOverrides> toMaterialParameters(lua_State* L, int index);

// **A surface shader parameter's value** (ADR 0091), to and from Luau: a
// number, a boolean (1 or 0), a `Vector2`, a `vector` or `Color3` (three), a
// table of four numbers, or -- where `texture` allows -- a texture's URN. False
// for anything else, and for a component that is not finite.
[[nodiscard]] bool readShaderParameterValue(lua_State* L, int index, asset::ShaderParameter& out, bool texture);
void pushShaderParameterValue(lua_State* L, const asset::ShaderParameter& parameter);

// One parameter's value: a `Color3` for `Color` and `Emissive`, a number for
// the rest.
void pushMaterialField(lua_State* L, asset::MaterialField field, const asset::MaterialProperties& values);
// Reads one into `into`, or false for a value of the wrong kind.
[[nodiscard]] bool readMaterialParameter(lua_State* L, int index, asset::MaterialField field,
                                         asset::MaterialProperties& into);

} // namespace luaug::script
