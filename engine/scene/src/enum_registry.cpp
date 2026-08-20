#include "luaug/scene/enum_registry.h"

#include <limits>

namespace luaug::scene
{

EnumRegistry::EnumRegistry()
{
    // Slot 0 is a placeholder, never an enum: it is what makes `EnumId` 0 mean
    // "no enum" for a zero-initialised `EnumValue` rather than meaning whichever
    // enum happened to be registered first.
    m_enums.emplace_back();
}

EnumId EnumRegistry::registerEnum(const EnumDescriptor& descriptor)
{
    if (m_byName.find(descriptor.name.id) != m_byName.end())
        return InvalidEnum;
    // A wrapped id would alias a live enum rather than fail, and every stored
    // `EnumValue` would then quietly mean an item of the wrong enum.
    if (m_enums.size() > static_cast<usize>(std::numeric_limits<EnumId>::max()))
        return InvalidEnum;

    const EnumId id = static_cast<EnumId>(m_enums.size());
    m_enums.push_back(descriptor);
    m_byName.emplace(descriptor.name.id, id);
    return id;
}

const EnumDescriptor* EnumRegistry::find(EnumId id) const noexcept
{
    if (id == InvalidEnum || static_cast<usize>(id) >= m_enums.size())
        return nullptr;
    return &m_enums[id];
}

EnumId EnumRegistry::findId(core::NameAtom name) const noexcept
{
    const auto found = m_byName.find(name.id);
    return found == m_byName.end() ? InvalidEnum : found->second;
}

const EnumItemDesc* EnumRegistry::findItem(EnumId id, core::NameAtom itemName) const noexcept
{
    const EnumDescriptor* descriptor = find(id);
    if (descriptor == nullptr || !itemName.valid())
        return nullptr;
    for (const EnumItemDesc& item : descriptor->items)
    {
        if (item.name == itemName)
            return &item;
    }
    return nullptr;
}

const EnumItemDesc* EnumRegistry::findValue(EnumId id, i32 value) const noexcept
{
    const EnumDescriptor* descriptor = find(id);
    if (descriptor == nullptr)
        return nullptr;
    for (const EnumItemDesc& item : descriptor->items)
    {
        if (item.value == value)
            return &item;
    }
    return nullptr;
}

} // namespace luaug::scene
