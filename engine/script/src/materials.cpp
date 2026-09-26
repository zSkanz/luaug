#include "luaug/script/materials.h"

#include "luaug/core/i18n.h"
#include "luaug/scene/world.h"
#include "luaug/script/binding.h"
#include "luaug/script/datatypes.h"

#include <lua.h>
#include <lualib.h>

#include <cmath>
#include <new>
#include <string>
#include <utility>

#include "class_descriptors.gen.h"

namespace luaug::script {
namespace {

using asset::MaterialField;

// The payload: which asset, and which clone of it in this VM's world. It owns a
// string, so it has a destructor -- and the destructor is where a clone's hold
// is given back.
struct MaterialData
{
    std::string source;
    u32 clone = 0;
};

constexpr int MaterialTag = static_cast<int>(UserdataTag::Material);

[[nodiscard]] MaterialData& checkMaterial(lua_State* L, int index)
{
    return *static_cast<MaterialData*>(luaL_checkudatatagged(L, index, MaterialTag));
}

[[nodiscard]] scene::World& world(lua_State* L)
{
    return *context(L).world;
}

void materialDtor(lua_State* L, void* userdata)
{
    auto* data = static_cast<MaterialData*>(userdata);
    // The VM closes before the world it runs over goes (`WorldHost` declares
    // them in that order), so the world is there to give the hold back to.
    if (data->clone != 0) {
        if (scene::World* over = context(L).world; over != nullptr)
            over->releaseMaterialClone(data->clone);
    }
    data->~MaterialData();
}

void pushHandle(lua_State* L, std::string source, u32 clone)
{
    void* memory = lua_newuserdatataggedwithmetatable(L, sizeof(MaterialData), MaterialTag);
    new (memory) MaterialData{std::move(source), clone};
    if (clone != 0)
        world(L).holdMaterialClone(clone);
}

[[nodiscard]] asset::ResolvedMaterial describe(lua_State* L, const MaterialData& self)
{
    scene::World& over = world(L);
    return over.resolveMaterial(over.atoms().intern(self.source), self.clone);
}

// --- Material.load -------------------------------------------------------------

int materialLoad(lua_State* L)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    const std::string content(text, length);
    // **Raises for a `Content` that names no material**, because a typo that
    // quietly drew the default would be found by looking at the screen.
    asset::MaterialLibrary* library = world(L).materialLibrary();
    if (!asset::isMaterialPath(content) || library == nullptr || !library->exists(content)) {
        const core::I18nArg args[] = {{"content", std::string_view{content}}};
        raise(L, LUAUG_TR("script.err.material_not_found"), args);
    }
    pushHandle(L, content, 0);
    return 1;
}

// --- Members -------------------------------------------------------------------

int materialGetSource(lua_State* L)
{
    const MaterialData& self = checkMaterial(L, 1);
    lua_pushlstring(L, self.source.data(), self.source.size());
    return 1;
}

template <MaterialField Field>
int materialGet(lua_State* L)
{
    const MaterialData& self = checkMaterial(L, 1);
    pushMaterialField(L, Field, describe(L, self).properties);
    return 1;
}

template <MaterialField Field>
int materialSet(lua_State* L)
{
    const MaterialData& self = checkMaterial(L, 1);
    const std::string_view name = asset::materialFieldName(Field);
    // **A loaded asset is read-only** (ADR 0090): one write would change every
    // part wearing it, in every scene that uses it, for the rest of the
    // session. `Clone` is the copy a script changes.
    if (self.clone == 0) {
        const core::I18nArg args[] = {{"property", name}, {"content", std::string_view{self.source}}};
        raise(L, LUAUG_TR("script.err.material_read_only"), args);
    }
    scene::MaterialClone* clone = world(L).writeMaterialClone(self.clone);
    if (clone == nullptr) {
        const core::I18nArg args[] = {{"content", std::string_view{self.source}}};
        raise(L, LUAUG_TR("script.err.material_clone_gone"), args);
    }
    asset::MaterialProperties values = clone->values;
    if (!readMaterialParameter(L, 3, Field, values)) {
        const core::I18nArg args[] = {{"property", name}};
        raise(L, LUAUG_TR("script.err.material_value_type"), args);
    }
    asset::copyMaterialField(Field, values, clone->values);
    clone->set |= asset::fieldBit(Field);
    // A map is a name, and the wire sends a name as this world's atom for it.
    if constexpr (Field == MaterialField::ColorMap || Field == MaterialField::NormalMap ||
                  Field == MaterialField::MetallicRoughnessMap || Field == MaterialField::EmissiveMap) {
        const std::string* written = Field == MaterialField::ColorMap      ? &values.colorMap
                                     : Field == MaterialField::NormalMap   ? &values.normalMap
                                     : Field == MaterialField::EmissiveMap ? &values.emissiveMap
                                                                           : &values.metallicRoughnessMap;
        if (!written->empty())
            (void)world(L).atoms().intern(*written);
    }
    return 0;
}

// --- Surface shader parameters (ADR 0091) -------------------------------------
//
// Methods rather than members: a shader's parameters are whatever its file
// declares, and a datatype's members are the IDL's closed set. What is read is
// what the MATERIAL says -- the asset, a variant's parents, a clone's changes --
// and nil where it leaves a parameter to the shader's own default.

int materialGetShaderParameter(lua_State* L)
{
    const MaterialData& self = checkMaterial(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    const asset::ResolvedMaterial resolved = describe(L, self);
    const asset::ShaderParameter* parameter = resolved.properties.shaderParameter(std::string_view{text, length});
    if (parameter == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    if (parameter->isTexture()) {
        lua_pushlstring(L, parameter->texture.data(), parameter->texture.size());
        return 1;
    }
    switch (parameter->components) {
    case 2:
        pushVector2(L, core::Vec2{parameter->value[0], parameter->value[1]});
        return 1;
    case 3:
        pushVector3(L, core::Vec3{parameter->value[0], parameter->value[1], parameter->value[2]});
        return 1;
    case 4:
        lua_createtable(L, 4, 0);
        for (int index = 0; index < 4; ++index) {
            lua_pushnumber(L, static_cast<double>(parameter->value[static_cast<usize>(index)]));
            lua_rawseti(L, -2, index + 1);
        }
        return 1;
    default:
        lua_pushnumber(L, static_cast<double>(parameter->value[0]));
        return 1;
    }
}

int materialSetShaderParameter(lua_State* L)
{
    const MaterialData& self = checkMaterial(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    const std::string_view name{text, length};
    if (self.clone == 0) {
        const core::I18nArg args[] = {{"property", name}, {"content", std::string_view{self.source}}};
        raise(L, LUAUG_TR("script.err.material_read_only"), args);
    }
    asset::ShaderParameter parameter;
    parameter.name = std::string(name);
    const int type = lua_type(L, 3);
    if (type == LUA_TNUMBER) {
        parameter.value[0] = static_cast<f32>(lua_tonumber(L, 3));
    }
    else if (type == LUA_TBOOLEAN) {
        parameter.value[0] = lua_toboolean(L, 3) != 0 ? 1.0f : 0.0f;
    }
    else if (type == LUA_TSTRING) {
        size_t size = 0;
        const char* urn = lua_tolstring(L, 3, &size);
        parameter.texture.assign(urn, size);
    }
    else if (lua_isvector(L, 3)) {
        const core::Vec3 v = checkVector3(L, 3);
        parameter.components = 3;
        parameter.value = {v.x, v.y, v.z, 0.0f};
    }
    else if (type == LUA_TUSERDATA && lua_userdatatag(L, 3) == static_cast<int>(UserdataTag::Color3)) {
        const core::Color3 c = checkColor3(L, 3);
        parameter.components = 3;
        parameter.value = {c.r, c.g, c.b, 0.0f};
    }
    else if (type == LUA_TUSERDATA && lua_userdatatag(L, 3) == static_cast<int>(UserdataTag::Vector2)) {
        const core::Vec2 v = checkVector2(L, 3);
        parameter.components = 2;
        parameter.value = {v.x, v.y, 0.0f, 0.0f};
    }
    else if (type == LUA_TTABLE && lua_objlen(L, 3) == 4) {
        parameter.components = 4;
        for (int index = 0; index < 4; ++index) {
            lua_rawgeti(L, 3, index + 1);
            parameter.value[static_cast<usize>(index)] = static_cast<f32>(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
    }
    else {
        const core::I18nArg args[] = {{"property", name}};
        raise(L, LUAUG_TR("script.err.material_value_type"), args);
    }
    for (const f32 component : parameter.value) {
        if (!std::isfinite(component)) {
            const core::I18nArg args[] = {{"property", name}};
            raise(L, LUAUG_TR("script.err.material_value_type"), args);
        }
    }
    scene::MaterialClone* clone = world(L).writeMaterialClone(self.clone);
    if (clone == nullptr) {
        const core::I18nArg args[] = {{"content", std::string_view{self.source}}};
        raise(L, LUAUG_TR("script.err.material_clone_gone"), args);
    }
    // A texture is a name the renderer finds by atom, as a map is.
    if (parameter.isTexture())
        (void)world(L).atoms().intern(parameter.texture);
    clone->values.setShaderParameter(std::move(parameter));
    return 0;
}

int materialClone(lua_State* L)
{
    const MaterialData& self = checkMaterial(L, 1);
    scene::World& over = world(L);
    // A clone of a clone is a clone of the same asset, carrying what the first
    // one changed -- `Source` never names another clone.
    asset::MaterialFieldMask set = 0;
    asset::MaterialProperties values;
    if (self.clone != 0) {
        if (const scene::MaterialClone* from = over.materialClone(self.clone); from != nullptr) {
            set = from->set;
            values = from->values;
        }
    }
    const u32 id = over.cloneMaterial(over.atoms().intern(self.source), set, values);
    pushHandle(L, self.source, id);
    return 1;
}

int materialEq(lua_State* L)
{
    const auto* a = static_cast<const MaterialData*>(lua_touserdatatagged(L, 1, MaterialTag));
    const auto* b = static_cast<const MaterialData*>(lua_touserdatatagged(L, 2, MaterialTag));
    lua_pushboolean(L, a != nullptr && b != nullptr && a->source == b->source && a->clone == b->clone);
    return 1;
}

int materialTostring(lua_State* L)
{
    const MaterialData& self = checkMaterial(L, 1);
    const std::string text = self.clone == 0
                                 ? "Material(" + self.source + ")"
                                 : "Material(" + self.source + ", clone " + std::to_string(self.clone) + ")";
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

template <MaterialField Field>
void addField(MemberTable& getters, MemberTable& setters, core::AtomTable& atoms)
{
    const std::string name(asset::materialFieldName(Field));
    addMember(getters, atoms, name.c_str(), materialGet<Field>);
    addMember(setters, atoms, name.c_str(), materialSet<Field>);
}

} // namespace

void registerMaterialTypes(lua_State* L)
{
    VmContext& ctx = context(L);
    core::AtomTable& atoms = ctx.world->atoms();
    MemberTable& getters = ctx.getters[static_cast<usize>(UserdataTag::Material)];
    MemberTable& setters = ctx.setters[static_cast<usize>(UserdataTag::Material)];
    addMember(getters, atoms, "Source", materialGetSource);
    addField<MaterialField::Color>(getters, setters, atoms);
    addField<MaterialField::Transparency>(getters, setters, atoms);
    addField<MaterialField::ColorMap>(getters, setters, atoms);
    addField<MaterialField::NormalMap>(getters, setters, atoms);
    addField<MaterialField::MetallicRoughnessMap>(getters, setters, atoms);
    addField<MaterialField::Emissive>(getters, setters, atoms);
    addField<MaterialField::EmissiveMap>(getters, setters, atoms);
    addField<MaterialField::Metalness>(getters, setters, atoms);
    addField<MaterialField::Roughness>(getters, setters, atoms);
    addField<MaterialField::NormalScale>(getters, setters, atoms);
    addField<MaterialField::AlphaMode>(getters, setters, atoms);
    addField<MaterialField::AlphaCutoff>(getters, setters, atoms);
    addField<MaterialField::DoubleSided>(getters, setters, atoms);

    MemberTable& methods = ctx.methods[static_cast<usize>(UserdataTag::Material)];
    addMember(methods, atoms, "Clone", materialClone);
    addMember(methods, atoms, "GetShaderParameter", materialGetShaderParameter);
    addMember(methods, atoms, "SetShaderParameter", materialSetShaderParameter);

    installTagMetatable(L, UserdataTag::Material, materialEq, materialTostring);
    lua_setuserdatadtor(L, MaterialTag, materialDtor);

    const luaL_Reg constructors[] = {{"load", materialLoad}, {nullptr, nullptr}};
    luaL_register(L, "Material", constructors);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);
}

void pushMaterial(lua_State* L, const scene::MaterialRef& material)
{
    pushHandle(L, material.source, material.clone);
}

std::optional<scene::MaterialRef> toMaterial(lua_State* L, int index)
{
    const auto* data = static_cast<const MaterialData*>(lua_touserdatatagged(L, index, MaterialTag));
    if (data == nullptr)
        return std::nullopt;
    return scene::MaterialRef{data->source, data->clone};
}

void pushMaterialField(lua_State* L, MaterialField field, const asset::MaterialProperties& values)
{
    switch (field) {
    case MaterialField::Color:
        pushColor3(L, values.color);
        return;
    case MaterialField::Emissive:
        pushColor3(L, values.emissive);
        return;
    case MaterialField::Transparency:
        lua_pushnumber(L, static_cast<double>(values.transparency));
        return;
    case MaterialField::Metalness:
        lua_pushnumber(L, static_cast<double>(values.metalness));
        return;
    case MaterialField::Roughness:
        lua_pushnumber(L, static_cast<double>(values.roughness));
        return;
    case MaterialField::NormalScale:
        lua_pushnumber(L, static_cast<double>(values.normalScale));
        return;
    case MaterialField::AlphaCutoff:
        lua_pushnumber(L, static_cast<double>(values.alphaCutoff));
        return;
    case MaterialField::ColorMap:
        lua_pushlstring(L, values.colorMap.data(), values.colorMap.size());
        return;
    case MaterialField::NormalMap:
        lua_pushlstring(L, values.normalMap.data(), values.normalMap.size());
        return;
    case MaterialField::MetallicRoughnessMap:
        lua_pushlstring(L, values.metallicRoughnessMap.data(), values.metallicRoughnessMap.size());
        return;
    case MaterialField::EmissiveMap:
        lua_pushlstring(L, values.emissiveMap.data(), values.emissiveMap.size());
        return;
    case MaterialField::AlphaMode:
        pushEnumItem(L, scene::EnumValue{static_cast<u16>(scene::generated::AlphaModeEnumId), values.alphaMode});
        return;
    case MaterialField::DoubleSided:
        lua_pushboolean(L, values.doubleSided);
        return;
    case MaterialField::Count:
        break;
    }
    lua_pushnil(L);
}

bool readMaterialParameter(lua_State* L, int index, MaterialField field, asset::MaterialProperties& into)
{
    const auto number = [&](f32& slot) {
        if (lua_type(L, index) != LUA_TNUMBER)
            return false;
        const double value = lua_tonumber(L, index);
        if (!std::isfinite(value))
            return false;
        slot = static_cast<f32>(value);
        return true;
    };
    const auto colour = [&](core::Color3& slot) {
        const std::optional<scene::Value> value = toValue(L, index, scene::ValueType::Color3);
        if (!value.has_value())
            return false;
        const core::Color3 c = std::get<core::Color3>(*value);
        if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b))
            return false;
        slot = c;
        return true;
    };
    const auto map = [&](std::string& slot) {
        if (lua_type(L, index) != LUA_TSTRING)
            return false;
        size_t length = 0;
        const char* text = lua_tolstring(L, index, &length);
        slot.assign(text, length);
        return true;
    };

    switch (field) {
    case MaterialField::Color:
        return colour(into.color);
    case MaterialField::Emissive:
        return colour(into.emissive);
    case MaterialField::Transparency:
        return number(into.transparency);
    case MaterialField::Metalness:
        return number(into.metalness);
    case MaterialField::Roughness:
        return number(into.roughness);
    case MaterialField::NormalScale:
        return number(into.normalScale);
    case MaterialField::AlphaCutoff:
        return number(into.alphaCutoff);
    case MaterialField::ColorMap:
        return map(into.colorMap);
    case MaterialField::NormalMap:
        return map(into.normalMap);
    case MaterialField::MetallicRoughnessMap:
        return map(into.metallicRoughnessMap);
    case MaterialField::EmissiveMap:
        return map(into.emissiveMap);
    case MaterialField::AlphaMode: {
        const std::optional<scene::Value> value = toValue(L, index, scene::ValueType::EnumItem);
        if (!value.has_value())
            return false;
        const scene::EnumValue item = std::get<scene::EnumValue>(*value);
        if (item.enumId != static_cast<u16>(scene::generated::AlphaModeEnumId))
            return false;
        into.alphaMode = item.value;
        return true;
    }
    case MaterialField::DoubleSided:
        if (!lua_isboolean(L, index))
            return false;
        into.doubleSided = lua_toboolean(L, index) != 0;
        return true;
    case MaterialField::Count:
        break;
    }
    return false;
}

void pushMaterialParameters(lua_State* L, const asset::MaterialOverrides& overrides)
{
    lua_createtable(L, 0, 4);
    const asset::MaterialProperties values = asset::overrideValues(overrides);
    for (usize index = 0; index < asset::MaterialFieldCount; ++index) {
        const auto field = static_cast<MaterialField>(index);
        if (!overrides.has(field))
            continue;
        pushMaterialField(L, field, values);
        const std::string name(asset::materialFieldName(field));
        lua_setfield(L, -2, name.c_str());
    }
}

std::optional<asset::MaterialOverrides> toMaterialParameters(lua_State* L, int index)
{
    if (lua_type(L, index) != LUA_TTABLE)
        return std::nullopt;
    const int table = lua_absindex(L, index);
    asset::MaterialOverrides out;
    asset::MaterialProperties values;
    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        // Key at -2, value at -1.
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 2);
            return std::nullopt;
        }
        size_t length = 0;
        const char* key = lua_tolstring(L, -2, &length);
        const std::optional<MaterialField> field = asset::materialFieldNamed(std::string_view{key, length});
        if (!field.has_value() || (asset::fieldBit(*field) & asset::DeclarableParameters) == 0 ||
            !readMaterialParameter(L, -1, *field, values)) {
            lua_pop(L, 2);
            return std::nullopt;
        }
        (void)asset::setOverride(out, *field, values);
        lua_pop(L, 1);
    }
    return out;
}

} // namespace luaug::script
