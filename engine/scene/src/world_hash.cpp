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
//   * **Never hash a struct's bytes.** Padding is not initialised by anything,
//     and its contents are whatever the allocation held before -- which is
//     stable within one process and different in the next. `CFrameD` carries
//     four such bytes and `EnumValue` two, and hashing them made this gate fail
//     across processes while passing twice in a row inside one. `pod()` below
//     now refuses any type that could have padding, so the next struct to grow
//     some fails to compile instead of failing at midnight.
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

namespace luaug::scene {
namespace {

class Hasher
{
public:
    Hasher() { XXH3_64bits_reset(&m_state); }

    void bytes(const void* data, usize size) noexcept { XXH3_64bits_update(&m_state, data, size); }

    template <class T>
    void pod(const T& value) noexcept
    {
        // `has_unique_object_representations` is exactly "every bit of this
        // object participates in its value", which is the precondition for
        // hashing bytes at all. It excludes anything with padding, and it
        // excludes floating point -- which is why floats have their own path.
        static_assert(std::has_unique_object_representations_v<T>,
                      "hashing a type with padding hashes uninitialised memory; hash its fields instead");
        bytes(&value, sizeof(T));
    }

    // Floats go in as their bit pattern, deliberately. -0.0 and 0.0 are the same
    // number and NOT the same state -- they divide differently -- so a
    // determinism hash that conflated them would call two worlds identical when
    // the next tick will tell them apart.
    void number(f32 value) noexcept { bytes(&value, sizeof(value)); }
    void number(f64 value) noexcept { bytes(&value, sizeof(value)); }

    void flag(bool value) noexcept { pod(static_cast<u8>(value ? 1 : 0)); }

    void vec3(const core::Vec3& value) noexcept
    {
        number(value.x);
        number(value.y);
        number(value.z);
    }

    void color3(const core::Color3& value) noexcept
    {
        number(value.r);
        number(value.g);
        number(value.b);
    }

    void vec2(const core::Vec2& value) noexcept
    {
        number(value.x);
        number(value.y);
    }

    void udim(const core::UDim& value) noexcept
    {
        number(value.scale);
        number(value.offset);
    }

    void cframe(const core::CFrameD& value) noexcept
    {
        number(value.position.x);
        number(value.position.y);
        number(value.position.z);
        for (const auto& row : value.rotation.m) {
            for (const f32 element : row)
                number(element);
        }
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

void hashValue(Hasher& hasher, const Value& value)
{
    const auto tag = static_cast<u8>(valueType(value));
    hasher.pod(tag);

    switch (valueType(value)) {
    case ValueType::Nil:
        break;
    case ValueType::Bool:
        hasher.flag(std::get<bool>(value));
        break;
    case ValueType::Number:
        hasher.number(std::get<f64>(value));
        break;
    case ValueType::String:
        hasher.text(std::get<std::string>(value));
        break;
    case ValueType::Vector3:
        hasher.vec3(std::get<core::Vec3>(value));
        break;
    case ValueType::CFrame:
        hasher.cframe(std::get<core::CFrameD>(value));
        break;
    case ValueType::Color3:
        hasher.color3(std::get<core::Color3>(value));
        break;
    case ValueType::Instance:
        hasher.pod(std::get<core::InstanceId>(value));
        break;
    case ValueType::EnumItem: {
        // Field by field: `EnumValue` is a u16 followed by an i32, and the two
        // bytes between them belong to no one.
        const EnumValue& item = std::get<EnumValue>(value);
        hasher.pod(item.enumId);
        hasher.pod(item.value);
        break;
    }
    case ValueType::Vector2:
        hasher.vec2(std::get<core::Vec2>(value));
        break;
    case ValueType::UDim:
        hasher.udim(std::get<core::UDim>(value));
        break;
    case ValueType::UDim2: {
        const core::UDim2& udim2 = std::get<core::UDim2>(value);
        hasher.udim(udim2.x);
        hasher.udim(udim2.y);
        break;
    }
    case ValueType::Rect: {
        const core::Rect& rect = std::get<core::Rect>(value);
        hasher.vec2(rect.min);
        hasher.vec2(rect.max);
        break;
    }
    }
}

} // namespace

u64 World::worldHash() const
{
    Hasher hasher;

    // Slot order, which is `SlotMap::forEach`'s documented order and a pure
    // function of the operation sequence.
    m_instances.forEach([&](core::InstanceId id, const InstanceRecord& record) {
        hasher.pod(id);
        hasher.flag(record.destroyed);

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
        if (const AttributeMap* attributes = m_attributes.find(id); attributes != nullptr) {
            hasher.pod(static_cast<u64>(attributes->size()));
            for (const auto& entry : *attributes) {
                hasher.text(m_atoms.text(entry.first));
                hashValue(hasher, entry.second);
            }
        }
        else {
            hasher.pod(u64{0});
        }

        if (const TagSet* tags = m_tags.find(id); tags != nullptr) {
            hasher.pod(static_cast<u64>(tags->size()));
            for (const core::NameAtom tag : *tags)
                hasher.text(m_atoms.text(tag));
        }
        else {
            hasher.pod(u64{0});
        }

        // Simulation state that is NOT a property, and therefore not covered by
        // the walk below (architecture.md §9: the hash is over sim-relevant
        // components AND physics state).
        //
        // Every field here is something the next tick's evolution depends on
        // and no script can read: an impulse queued for a tick that has not run,
        // the command a character was given, the vertical velocity a controller
        // carries, and which bodies the solver has put to sleep. Two runs
        // agreeing on every visible number while disagreeing on one of these are
        // one tick from disagreeing on all of them.
        if (const RigidBodyComponent* body = m_rigidBodies.find(id); body != nullptr) {
            hasher.vec3(body->pendingImpulse);
            hasher.flag(body->active);
        }
        if (const CharacterBodyComponent* character = m_characterBodies.find(id); character != nullptr) {
            hasher.vec3(character->moveDirection);
            hasher.flag(character->jumpRequested);
            hasher.number(character->verticalVelocity);
            hasher.pod(character->groundPart);
        }
        // An action's resolved value is simulation state and no property
        // exposes it -- `GetState` is a METHOD, so the walk below cannot see it.
        // Leaving it out would let two runs agree on every hashed number while
        // one of them had a key held, which is one tick from disagreeing about
        // everything the player did next.
        if (const InputActionComponent* action = m_inputActions.find(id); action != nullptr) {
            hasher.vec3(action->axis);
            hasher.flag(action->pressed);
        }

        // **The terrain field, which no property can carry** (ADR 0067). It is
        // megabytes of samples, so it is not in the walk below and has to be
        // here -- and three rules decide how, each of which is a way to get it
        // silently wrong.
        //
        // **Per-object digests, not bytes.** Every tile and brick computes an
        // xxh3 of its arrays once, at construction, so this is O(objects)
        // instead of O(bytes): a cell of a hundred tiles costs a hundred
        // eight-byte reads rather than half a megabyte, on every tick that hashes.
        //
        // **The key goes in beside the digest**, because two fields holding the
        // same ground in different places are different fields, and a digest over
        // contents alone would miss a move.
        //
        // **Nothing about the SHARING is hashed** -- no pointer, no refcount, no
        // address. Two runs that share a brick and two runs that cloned it hash
        // identically, which is the property that lets copy-on-write be an
        // implementation detail rather than part of the world.
        //
        // The representation itself is state and is covered for free: which
        // columns carry bricks decides which digests appear here, promotion
        // happens exactly at an edit, demotion only at an explicit `Compact`,
        // and `.lterrain` preserves it byte for byte -- so a save and a reload
        // hash the same. An authored world carries that format base64'd in the
        // scene's own `terrain` key rather than in a file beside it, because
        // `writeScene` returns a string and a scene is one text file; a streamed
        // one will carry the same bytes per cell.
        if (const TerrainComponent* terrain = m_terrains.find(id); terrain != nullptr) {
            hasher.number(static_cast<f64>(terrain->field.settings().voxelSize));
            hasher.number(static_cast<f64>(terrain->minHeight));
            hasher.number(static_cast<f64>(terrain->maxHeight));
            for (const asset::TileKey key : terrain->field.tileKeys()) {
                hasher.pod(key.x);
                hasher.pod(key.z);
                if (const asset::HeightTile* tile = terrain->field.findTile(key); tile != nullptr)
                    hasher.pod(tile->digest);
            }
            for (const asset::BrickKey key : terrain->field.brickKeys()) {
                hasher.pod(key.x);
                hasher.pod(key.y);
                hasher.pod(key.z);
                if (const asset::Brick* brick = terrain->field.findBrick(key); brick != nullptr)
                    hasher.pod(brick->digest);
            }
        }

        // Class-specific state, reached through the same generated accessors a
        // script would use. Hashing the components directly would be faster and
        // would silently stop covering a property whose storage moved.
        if (const ClassDescriptor* descriptor = m_classes.find(record.classId); descriptor != nullptr) {
            for (const ClassDescriptor* current = descriptor; current != nullptr;
                 current = m_classes.find(current->super)) {
                for (const PropertyDesc& property : current->properties) {
                    if (property.get == nullptr)
                        continue;
                    // A fact about the host, not about the world -- the engine
                    // version, the Luau version, the display's scale. Excluded
                    // for the same reason a `ClassId` is above: it is a property
                    // of the build or the machine rather than of the operation
                    // sequence R10 says this hash is a function of.
                    //
                    // Found by bumping the engine to 1.1.0, which moved every
                    // determinism trace at tick zero. The churn is not the
                    // problem; what it HIDES is. A release expected to move
                    // every trace is a release in which a real determinism
                    // regression looks exactly like the expected movement.
                    if (property.hostFact)
                        continue;
                    hasher.text(m_atoms.text(property.name));
                    hashValue(hasher, property.get(*this, id));
                }
            }
        }
    });

    return hasher.digest();
}

} // namespace luaug::scene
