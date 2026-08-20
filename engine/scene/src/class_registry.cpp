#include "luaug/scene/class_registry.h"

#include <limits>

namespace luaug::scene {
namespace {

// Five of the six public accessors need "the entry for this id, or nothing",
// and `Entry` is private so a plain free function cannot name it. Deducing the
// container type sidesteps that -- access control governs names, not types --
// and keeps the bounds guard in one place, which is where a guard repeated five
// times ends up subtly different in one of them.
template <class Entries>
auto entryAt(Entries& entries, ClassId id) noexcept -> decltype(&entries[0])
{
    if (id == InvalidClass || static_cast<usize>(id) >= entries.size())
        return nullptr;
    return &entries[id];
}

} // namespace

ClassRegistry::ClassRegistry()
{
    // Slot 0 is a placeholder, never a class: it is what makes `ClassId` 0 mean
    // "no class" for a zero-initialised `ClassComp` rather than meaning
    // whichever class happened to be registered first.
    m_classes.emplace_back();
}

ClassId ClassRegistry::registerClass(const ClassDescriptor& descriptor)
{
    if (descriptor.super != InvalidClass && find(descriptor.super) == nullptr)
        return InvalidClass;
    if (m_byName.find(descriptor.name.id) != m_byName.end())
        return InvalidClass;
    // A wrapped id would alias a live class rather than fail, and every
    // `IsA` in the world would then quietly answer for the wrong one.
    if (m_classes.size() > static_cast<usize>(std::numeric_limits<ClassId>::max()))
        return InvalidClass;

    const ClassId id = static_cast<ClassId>(m_classes.size());
    Entry& entry = m_classes.emplace_back();
    entry.descriptor = descriptor;
    entry.ancestry.push_back(id);

    if (descriptor.super != InvalidClass) {
        // Both of these are why registration insists the super came first: the
        // super's ancestry and lookup maps are already complete, so this is a
        // copy rather than a walk, and it is paid once per class instead of
        // once per `IsA` and once per property access.
        const Entry& super = m_classes[descriptor.super];
        entry.ancestry.insert(entry.ancestry.end(), super.ancestry.begin(), super.ancestry.end());
        entry.properties = super.properties;
        entry.methods = super.methods;
        entry.events = super.events;
    }

    // Overlaid after the copy, so a class that redeclares an inherited member
    // shadows it. The pointers are into the generated static storage the spans
    // view, which outlives the registry.
    //
    // A shadowing property keeps the slot it inherited: the slot identifies the
    // member, and a subscription made through the base must survive the derived
    // class redeclaring it.
    u16 nextSlot = static_cast<u16>(entry.properties.size());
    for (const PropertyDesc& property : entry.descriptor.properties) {
        PropertyEntry& slot = entry.properties[property.name.id];
        slot.descriptor = &property;
        if (slot.slot == NoSlot)
            slot.slot = nextSlot++;
    }
    for (const MethodDesc& method : entry.descriptor.methods)
        entry.methods[method.name.id] = &method;
    for (const EventDesc& event : entry.descriptor.events)
        entry.events[event.name.id] = &event;

    m_byName[descriptor.name.id] = id;
    return id;
}

const ClassDescriptor* ClassRegistry::find(ClassId id) const noexcept
{
    const Entry* entry = entryAt(m_classes, id);
    return entry == nullptr ? nullptr : &entry->descriptor;
}

ClassId ClassRegistry::findId(core::NameAtom name) const noexcept
{
    const auto it = m_byName.find(name.id);
    return it == m_byName.end() ? InvalidClass : it->second;
}

bool ClassRegistry::isA(ClassId derived, ClassId base) const noexcept
{
    const Entry* entry = entryAt(m_classes, derived);
    if (entry == nullptr || base == InvalidClass)
        return false;

    for (const ClassId ancestor : entry->ancestry) {
        if (ancestor == base)
            return true;
    }
    return false;
}

const PropertyDesc* ClassRegistry::findProperty(ClassId id, core::NameAtom name) const noexcept
{
    const Entry* entry = entryAt(m_classes, id);
    if (entry == nullptr)
        return nullptr;

    const auto it = entry->properties.find(name.id);
    return it == entry->properties.end() ? nullptr : it->second.descriptor;
}

u16 ClassRegistry::propertySlot(ClassId id, core::NameAtom name) const noexcept
{
    const Entry* entry = entryAt(m_classes, id);
    if (entry == nullptr)
        return NoSlot;

    const auto it = entry->properties.find(name.id);
    return it == entry->properties.end() ? NoSlot : it->second.slot;
}

const MethodDesc* ClassRegistry::findMethod(ClassId id, core::NameAtom name) const noexcept
{
    const Entry* entry = entryAt(m_classes, id);
    if (entry == nullptr)
        return nullptr;

    const auto it = entry->methods.find(name.id);
    return it == entry->methods.end() ? nullptr : it->second;
}

const EventDesc* ClassRegistry::findEvent(ClassId id, core::NameAtom name) const noexcept
{
    const Entry* entry = entryAt(m_classes, id);
    if (entry == nullptr)
        return nullptr;

    const auto it = entry->events.find(name.id);
    return it == entry->events.end() ? nullptr : it->second;
}

} // namespace luaug::scene
