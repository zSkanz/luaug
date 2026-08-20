// WorldHash (architecture.md §9, ADR 0025).
//
// One number that says whether two runs produced the same world. The gate it
// serves is level B: same engine build, same platform, same seed, same inputs
// and same tick configuration produce the same hash -- so what is hashed has to
// be a pure function of the operation sequence and nothing else.
//
// Two rules follow from that and neither is optional:
//
//   * **Never hash a `NameAtom`'s number.** It depends on the order strings
//     were interned, which depends on the order things were built. The text is
//     hashed instead. This is the single easiest way to write a hash that
//     reproduces perfectly on one machine and disagrees on another.
//   * **Never iterate an unordered container into the hash.** Instances are
//     walked in slot order and children in sibling order; the tag map is
//     reached per instance rather than swept.
//
// Instance ids ARE hashed, including their slot index. That is deliberate:
// slot allocation is itself a pure function of the operation sequence (the
// SlotMap's free list is LIFO and nothing else touches it), so two runs of the
// same script assign the same slots. It is also the cheap answer -- the
// alternative is assigning stable ordinals by traversal, which costs a pass and
// buys nothing at level B.
#include "luaug/scene/world.h"

// Header-only: everything xxHash needs is inlined into this translation unit,
// so there is no library to build, link or explain in a preset.
#define XXH_INLINE_ALL
#include "xxhash.h"

namespace luaug::scene
{
namespace
{

class Hasher
{
public:
    Hasher() { XXH3_64bits_reset(&m_state); }

    void bytes(const void* data, usize size) noexcept { XXH3_64bits_update(&m_state, data, size); }

    template <class T>
    void pod(const T& value) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>, "only trivially copyable values are hashed by their bytes");
        bytes(&value, sizeof(T));
    }

    void text(std::string_view value) noexcept
    {
        // The length goes in as well, so that ("ab", "c") cannot hash the same
        // as ("a", "bc") -- the classic way a concatenating hash loses a
        // boundary it was meant to keep.
        const u64 size = value.size();
        pod(size);
        bytes(value.data(), value.size());
    }

    [[nodiscard]] u64 digest() const noexcept { return XXH3_64bits_digest(&m_state); }

private:
    XXH3_state_t m_state{};
};

void hashValue(Hasher& hasher, const Value& value, const core::AtomTable& atoms)
{
    const auto tag = static_cast<u8>(valueType(value));
    hasher.pod(tag);

    switch (valueType(value))
    {
    case ValueType::Nil:
        break;
    case ValueType::Bool:
        hasher.pod(std::get<bool>(value));
        break;
    case ValueType::Number:
        hasher.pod(std::get<f64>(value));
        break;
    case ValueType::String:
        hasher.text(std::get<std::string>(value));
        break;
    case ValueType::Vector3:
        hasher.pod(std::get<core::Vec3>(value));
        break;
    case ValueType::CFrame:
        hasher.pod(std::get<core::CFrameD>(value));
        break;
    case ValueType::Color3:
        hasher.pod(std::get<core::Color3>(value));
        break;
    case ValueType::Instance:
        hasher.pod(std::get<core::InstanceId>(value));
        break;
    case ValueType::EnumItem:
        hasher.pod(std::get<EnumValue>(value));
        break;
    }
    (void)atoms;
}

} // namespace

u64 World::worldHash() const
{
    Hasher hasher;

    // Slot order, which is `SlotMap::forEach`'s documented order and a pure
    // function of the operation sequence.
    m_instances.forEach([&](core::InstanceId id, const InstanceRecord& record) {
        hasher.pod(id);
        hasher.pod(record.destroyed);

        // The class is hashed by NAME, not by `ClassId`: an id depends on
        // registration order, and registration order is a property of the
        // engine build rather than of the world.
        if (const ClassDescriptor* descriptor = m_classes.find(record.classId); descriptor != nullptr)
            hasher.text(m_atoms.text(descriptor->name));
        else
            hasher.text({});

        hasher.text(m_atoms.text(record.name));
        hasher.pod(record.parent);
        hasher.pod(record.childCount);

        // Sibling order is observable through `GetChildren`, so it is part of
        // the state a replay has to reproduce.
        for (core::InstanceId child = record.firstChild; child.valid(); child = nextSibling(child))
            hasher.pod(child);

        // Insertion-ordered by construction (see `AttributeMap`), so this walk
        // is stable without sorting.
        if (const AttributeMap* attributes = m_attributes.find(id); attributes != nullptr)
        {
            hasher.pod(static_cast<u64>(attributes->size()));
            for (const auto& entry : *attributes)
            {
                hasher.text(m_atoms.text(entry.first));
                hashValue(hasher, entry.second, m_atoms);
            }
        }
        else
        {
            hasher.pod(u64{0});
        }

        if (const TagSet* tags = m_tags.find(id); tags != nullptr)
        {
            hasher.pod(static_cast<u64>(tags->size()));
            for (const core::NameAtom tag : *tags)
                hasher.text(m_atoms.text(tag));
        }
        else
        {
            hasher.pod(u64{0});
        }

        // Class-specific state, reached through the same generated accessors a
        // script would use. Hashing the components directly would be faster and
        // would silently stop covering a property whose storage moved.
        if (const ClassDescriptor* descriptor = m_classes.find(record.classId); descriptor != nullptr)
        {
            for (const ClassDescriptor* current = descriptor; current != nullptr;
                 current = m_classes.find(current->super))
            {
                for (const PropertyDesc& property : current->properties)
                {
                    if (property.get == nullptr)
                        continue;
                    hasher.text(m_atoms.text(property.name));
                    hashValue(hasher, property.get(*this, id), m_atoms);
                }
            }
        }
    });

    return hasher.digest();
}

} // namespace luaug::scene
